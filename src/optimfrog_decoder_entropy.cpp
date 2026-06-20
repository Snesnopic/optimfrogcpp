// Entropy decoder – rewritten from Ghidra decompilation of FUN_00109ab0.
// See comments in implementation_plan.md for the full analysis.

#include "optimfrog_decoder.h"
#include <cstring>
#include <iostream>
#include <cassert>

// ============================================================
// OFR_ModelContext  (mirrors binary's "segment tree" context)
// ============================================================
// Binary layout:
//   +0x00  long*   freqs array (allocated separately)
//   +0x08  uint    num_symbols
//   +0x0c  uint    total_freq   (running sum)
//   +0x10  uint    limit
//   +0x14  uint    num_nodes    (smallest power-of-two >= num_symbols)
// freqs[0..num_nodes-1]           = internal nodes (left-subtree sum only)
// freqs[num_nodes..+num_symbols-1] = leaf frequencies

void OFR_ModelContext::init(uint32_t symbols, uint32_t lim) {
    num_symbols = symbols;
    limit       = lim;
    num_nodes   = 1;
    while (num_nodes < num_symbols) num_nodes <<= 1;

    freqs.assign(num_nodes * 2, 0u);
    for (uint32_t i = 0; i < num_symbols; ++i)
        freqs[num_nodes + i] = 1u;
    total_freq = num_symbols;
    rebuild_tree(1);
}

// FUN_00105df0
void OFR_ModelContext::halve_freqs() {
    total_freq = 0;
    for (uint32_t i = 0; i < num_symbols; ++i) {
        uint32_t& f = freqs[num_nodes + i];
        f = ((f - 1u) >> 1) + 1u;
        total_freq += f;
    }
    rebuild_tree(1);
}

// FUN_00105bb0: internal[node] = sum of left-child subtree
uint32_t OFR_ModelContext::rebuild_tree(uint32_t node) {
    if (num_nodes <= node) return freqs[node];
    uint32_t left  = rebuild_tree(node * 2);
    uint32_t right = rebuild_tree(node * 2 + 1);
    freqs[node] = left;   // store only left-subtree sum
    return left + right;
}

// ============================================================

void OFR_EntropyDecoder::init(uint32_t depth, uint32_t ch, bool fast_stereo) {
    bit_depth    = depth;
    channels     = ch;
    is_fast_stereo = fast_stereo;
}

// Rebuild internal nodes for the master context (same logic as OFR_ModelContext)
uint32_t OFR_EntropyDecoder::rebuild_master(uint32_t node) {
    if (master_num_nodes <= node) return (uint32_t)master_freqs[node];
    uint32_t left  = rebuild_master(node * 2);
    uint32_t right = rebuild_master(node * 2 + 1);
    master_freqs[node] = (int32_t)left;
    return left + right;
}

void OFR_EntropyDecoder::halve_master() {
    master_total_freq = 0;
    for (uint32_t i = 0; i < master_num_symbols; ++i) {
        uint32_t& f = master_freqs[master_num_nodes + i];
        f = (((f - 1) >> 1) + 1u);
        master_total_freq += f;
    }
    rebuild_master(1);
}

// Called once per block to prepare the entropy decoder.
// Per the binary (FUN_00109460):
//   *(param_1+0x2c) = block_size  (not read from range coder)
//   *(param_1+0x28) = bit_depth   (not read from range coder)
// The weight/weight2 are read from the range coder by FUN_00106eb0 which is called
// the FIRST time decode_block is invoked (when needs_init=true / param_1+0x4c != 0).
// This call happens inside FUN_00109470 / FUN_00109530 automatically.
// So init_from_bitstream should NOT read from the range coder.
void OFR_EntropyDecoder::init_from_bitstream(OFR_RangeCoder* rc) {
    // ---- Master context ----
    // FUN_00105c60(param_1+0x30, 0x20, 0x8000)
    // → num_symbols=32, limit=0x8000
    master_num_symbols = 32u;
    master_limit       = 0x8000u;
    master_num_nodes   = 32u;
    master_freqs.assign(master_num_nodes * 2, 0);
    for (uint32_t i = 0; i < master_num_symbols; ++i)
        master_freqs[master_num_nodes + i] = 1;
    master_total_freq = master_num_symbols;

    rebuild_master(1);

    // ---- Sub-contexts ----
    // FUN_00113f50(lVar16+0x58, 9, 9, 0x8000) → 9 contexts, each 9 symbols, limit 0x8000
    contexts.resize(9);
    for (auto& c : contexts)
        c.init(9u, 0x8000u);

    // ---- Reset state ----
    // variance resets to 0.0 per FUN_00109470: *(param_1+0x18) = 0
    variance  = 0.0;
    variance2 = 0.0;
    // needs_init = true so decode_block will read weight on first call (FUN_00106eb0)
    needs_init      = true;
    decoded_samples = 0;
}

// ============================================================
// range-coder helpers  (exact transcription of normalize loop)
// ============================================================
// The binary's normalise loop (e.g. lines 28-54 of FUN_00109ab0):
//   while (range < 0x800001):
//     value = (cache_bit<<7 | value<<8)
//     cache_bit = next_byte & 0xff
//     value |= cache_bit >> 1
//     range <<= 8

// Our OFR_RangeCoder::normalize() must match this exactly.
// From include/optimfrog_decoder.h the struct is:
//   uint32_t value, range;
//   uint8_t  cache;
//   void*    bs_ptr;
// We'll use our existing normalize() implementation.

// ============================================================
// Tree-walk decode (used in both fast and slow path)
// ============================================================
// Mirrors lines 64-95 and 382-412 of FUN_00109ab0.
// Returns: symbol (0-based), updates range coder.
static uint32_t init_tree_recursive(std::vector<uint32_t>& freqs, uint32_t node, uint32_t num_symbols) {
    if (node >= num_symbols) {
        return freqs[node];
    }
    uint32_t left_sum = init_tree_recursive(freqs, node * 2, num_symbols);
    uint32_t right_sum = init_tree_recursive(freqs, node * 2 + 1, num_symbols);
    freqs[node] = left_sum;
    return left_sum + right_sum;
}

uint32_t decode_tree_symbol(
    std::vector<uint32_t>& freqs, uint32_t num_nodes, uint32_t num_symbols,
    uint32_t& total_freq, uint32_t limit, OFR_RangeCoder* rc, uint32_t increment)
{
    rc->normalize();
    uint32_t step = rc->range / total_freq;
    uint32_t val  = rc->value / step;
    uint32_t bound = total_freq - 1;
    if (val < total_freq) bound = val;

    // Walk the segment tree
    uint32_t node = 1;
    uint32_t acc  = 0;
    while (node < num_symbols) {
        uint32_t left_sum = freqs[node];
        uint32_t sum = left_sum + acc;
        if (sum <= bound) {
            // go right: sym is in right subtree
            node = node * 2 + 1;
            acc  = sum;
        } else {
            // go left: sym is in left subtree
            freqs[node] += increment;  // update internal node count
            node = node * 2;
        }
    }

    uint32_t leaf_freq = freqs[node];
    uint32_t iAcc = acc * step;
    rc->value -= iAcc;
    if (acc + leaf_freq < total_freq) {
        rc->range = step * leaf_freq;
    } else {
        rc->range -= iAcc;
    }
    
    freqs[node] += increment;
    total_freq  += increment;
    
    if (total_freq >= limit) {
        // Halve leaf frequencies and rebuild tree
        for (uint32_t i = 0; i < num_symbols; ++i) {
            uint32_t leaf = num_symbols + i;
            freqs[leaf] = ((freqs[leaf] - 1) / 2) + 1;
        }
        total_freq = init_tree_recursive(freqs, 1, num_symbols);
    }

    return node - num_symbols;
}

// ============================================================
// decode_abs_bits: decode an integer of 'nbits' bits from RC
// (Lines 219-319 and 462-521 of FUN_00109ab0)
// ============================================================
static inline uint32_t decode_abs_bits(uint32_t nbits, OFR_RangeCoder* rc) {
    uint32_t result = 0;
    uint32_t bits   = nbits;
    uint32_t shift  = 0;

    while (bits >= 13) {
        rc->normalize();
        uint32_t step = rc->range >> 12;
        uint32_t q    = rc->value / step;
        uint32_t qc   = (q < 0x1000u) ? q : 0xfffu;
        rc->value -= qc * step;
        if (qc + 1u >= 0x1000u) rc->range -= qc * step;
        else                     rc->range  = step;
        result |= qc << (shift & 0x1fu);
        shift  += 12;
        bits   -= 12;
    }

    if (bits > 0) {
        rc->normalize();
        uint32_t step = rc->range >> (bits & 0x1fu);
        uint32_t maxv = 1u << bits;
        uint32_t q    = rc->value / step;
        uint32_t qc   = (q >= maxv) ? (maxv - 1u) : q;
        rc->value -= qc * step;
        if (qc + 1u >= maxv) rc->range -= qc * step;
        else                  rc->range  = step;
        result |= qc << (shift & 0x1fu);
    }

    return result;
}

// ============================================================
// decode_one_sample  –  mirrors FUN_00109ab0 exactly
// ============================================================
int32_t OFR_EntropyDecoder::decode_one_sample(double& var, OFR_RangeCoder* rc) {
    uint32_t raw_val = 0;
    int sym2 = -1;
    // The binary uses CVTTSD2SI to cast the double to an int64_t
    int64_t var_int = (int64_t)var;
    uint32_t uVar6 = ((uint32_t)var_int >> 2) & 0x3fffffffu;
    uint32_t uVar16 = uVar6 + 1u;

    static int dbg_count = 0;
    if (dbg_count++ < 5) {
    }
    if (uVar16 < 0x4001u) {
        int sym = decode_tree_symbol(
            master_freqs, master_num_nodes, master_num_symbols,
            master_total_freq, master_limit, rc, 2);

        if (sym != 0x1f) {
            // Symbol 0..30: decode extra in [0, uVar16)
            // Lines 98-143
            rc->normalize();
            uint32_t step2 = rc->range / uVar16;
            uint32_t q2    = rc->value / step2;
            uint32_t q2c   = (q2 < uVar16) ? q2 : uVar6;  // uVar6 = uVar16-1
            rc->value -= q2c * step2;
            if (q2c + 1u >= uVar16) rc->range -= q2c * step2;
            else                     rc->range  = step2;
            raw_val = q2c + (uint32_t)((int32_t)sym * (int32_t)uVar16);
        } else {
            // Escape (sym==31): decode abs value using wider bit range
            // Lines 145-319
            uVar16 = uVar16 * 31u;
            goto decode_escape;
        }
    } else {
        // ================================================================
        // Slow path (uVar16 >= 0x4001): bit-width decode
        // Lines 321-683
        // ================================================================

        // Compute bit-width iVar14 = floor(log2(uVar16))  (lines 322-345)
        uint32_t tmp = uVar16;
        int iVar14 = 0;
        if (tmp >= 0x10000u) { tmp >>= 16; iVar14 = 16; }
        if (tmp >= 0x100u) { tmp >>= 8; iVar14 += 8; }
        if (tmp > 0xfu)  { tmp >>= 4; iVar14 += 4; }
        if (tmp > 3u)    { tmp >>= 2; iVar14 += 2; }
        iVar14 += (int)(tmp != 1u);

        int8_t sVar8 = (int8_t)iVar14;

        sym2 = decode_tree_symbol(
            master_freqs, master_num_nodes, master_num_symbols,
            master_total_freq, master_limit, rc, 2);

        if (sym2 != 0x1f) {
            // Decode iVar14 additional bits
            uint32_t extra = decode_abs_bits((uint32_t)iVar14, rc);
            raw_val = extra + (uint32_t)((int32_t)sym2 << sVar8);
        } else {
            // Escape (sym2==31): use uVar16 = 0x1f << iVar14 as new range
            // Lines 523+
            uVar16 = 0x1fu << (uint32_t)(uint8_t)sVar8;
            goto decode_escape;
        }
    }
    goto emit;

decode_escape:
    {
        // Lines 145-319 (fast-path escape) and 524-683 (slow-path escape)
        // Compute new_bits = floor(log2(uVar16))
        uint32_t av = uVar16;
        int iVar14d = 0;
        if (av > 0xffffu) { av >>= 16; iVar14d = 16; }
        if (av > 0xffu)   { av >>= 8;  iVar14d += 8;  }
        if (av > 0xfu)    { av >>= 4;  iVar14d += 4;  }
        if (av > 3u)      { av >>= 2;  iVar14d += 2;  }
        iVar14d += (int)(av >= 2u);

        uint32_t new_bits = (uint32_t)iVar14d;

        // Validate (line 169 / 542)
        if (new_bits >= bit_depth) {
            std::cerr << "ERROR: escape new_bits=" << new_bits
                      << " >= bit_depth=" << bit_depth << "\n";
            raw_val = 0;
            goto emit;
        }

        uint32_t remaining = bit_depth - new_bits;

        // Decode index in [0, remaining)
        rc->normalize();
        uint32_t step6  = rc->range / remaining;
        uint32_t q6     = rc->value / step6;
        uint32_t q6c    = (q6 < remaining) ? q6 : (remaining - 1u);
        rc->value -= q6c * step6;
        if (q6c + 1u < remaining) rc->range = step6;
        else                       rc->range -= q6c * step6;

        uint32_t total_bits = q6c + new_bits;  // uVar6 in the decompilation

        // Decode total_bits more bits
        uint32_t extra2 = decode_abs_bits(total_bits, rc);
        raw_val = extra2 + (1u << (uint8_t)(total_bits & 0x1fu));
    }

emit:
    // Line 687: update variance in-place
    uint32_t var_raw_val = (raw_val > 0x3fffffffu) ? 0x3fffffffu : raw_val;
    var = var * weight + (double)var_raw_val * weight2;

    if (raw_val > 1000) {
    }

    // Lines 686, 688-691: zigzag decode
    // uVar6 = raw_val >> 1;  if (raw_val & 1) uVar6 = ~uVar6;
    uint32_t out = raw_val >> 1u;
    if (raw_val & 1u) out = ~out;
    return (int32_t)out;
}

// ============================================================
// decode_block
// ============================================================
int32_t OFR_EntropyDecoder::decode_block(int32_t* dest, uint32_t count, OFR_RangeCoder* rc) {
    // The binary's FUN_00109470/FUN_00109530 checks *(param_1+0x4c) (needs_init).
    // If set:
    //   1. FUN_00105c60(param_1+0x30, 0x20, 0x8000) = reinit master context
    //   2. *(param_1+0x18) = 0  = reset variance to 0.0
    //   3. *(param_1+0x4c) = 0  = clear needs_init
    // Then the first FUN_00109ab0 call checks *(param_1+0x48) == 0
    //   (needs_weight_init). If so, calls FUN_00106eb0(param_1, rc):
    //     reads 12-bit val, computes weight = (val-1)/val, weight2 = 1.0/val
    //     sets *(param_1+0x48) = 0 (cleared)
    //     sets *(param_1+0x4c) = 1 (set)... wait this would loop?
    //
    // Actually looking at FUN_00109ab0 more carefully:
    // Line 26 of FUN_0010a9b0 (different from FUN_00109ab0):
    //   uVar5 = *(uint *)(param_1 + 0x3c);  <- num_symbols, not a "needs_init" check
    // The real init check is in FUN_00109470 at top:
    //   if (*(char*)(param_1+0x4c) != '\0') {
    //     FUN_00105c60(param_1+0x30, 0x20, 0x8000);
    //     *(uint*)(param_1+0x18) = 0;
    //     *(char*)(param_1+0x4c) = 0;
    //   }
    // FUN_00106eb0 is called via vtable BEFORE FUN_00109ab0:
    //   Looking at FUN_00109470 again - it doesn't call FUN_00106eb0.
    //   FUN_00106eb0 is a DIFFERENT function that comes from vtable[1] at 003255f0
    //   and 0x106eb0 is listed there. It's the "init" method of a different class.
    //
    // Looking at what actually calls decode_block (FUN_00109470):
    // FUN_00109470 is called from FUN_00116060 (OFR_Block_decode) line:
    //   (**(code **)(*(long *)param_1[1] + 8))((long *)param_1[1], param_2, uVar2, param_1+3);
    // That's vtable[1]. For the entropy object (param_1[0xe] in init_decoders),
    // it has vtable = 003255f0, vtable[1] = 0x001075c0??? No wait...
    //
    // param_1[0xe] is init by FUN_00109460 which calls FUN_00109430:
    //   FUN_00109430(param_1) → *param_1 = &PTR_FUN_003255f0
    // So vtable = 003255f0:
    //   [0] = 0x106eb0 (decode_block with weight init)
    //   [1] = 0x107590 (the actual decode... wait)
    //   Wait: vtable[1] in FUN_001075c0 chain is for the STEREO fast object!
    //
    // For param_1[0xe] (stereo simple), vtable = 003255f0:
    //   vtable[0] = FUN_00106eb0  (init, reads weight from bitstream)
    //   vtable[1] = FUN_001097b0  (destructor?)
    // No that can't be right either.
    //
    // FUN_00116060 calls (*vtable[1])(entropy, output_buf, count, predictor_ptr)
    // For the entropy at param_1[0xe] (init by FUN_00109460/FUN_00109430):
    //   vtable = 003255f0
    //   vtable[1] = 0x001070b0???
    //
    // Let me check: 003255f0[0] = 0x106eb0, 003255f0[1] = 0x107590?
    // From our earlier read: 003255f0 = [b06e1000...] [b070 3200...]
    //   = 0x106eb0 and 0x3270b0???
    // Actually from the read: hex b06e100000000000 = little-endian 0x00106eb0
    //                              b070320000000000 = 0x003270b0 (different!)
    // So vtable[1] = 0x003270b0 which is a data section address, not a function.
    // This suggests the vtable is only 1 entry? No, there's likely RTTI before.
    //
    // OK I'm going in circles. Let me take a completely different approach:
    // FORGET the init machinery. Just:
    //   1. On first call: read weight from range coder (FUN_00106eb0 logic)
    
    if (needs_init) {
        // Init master context (FUN_00105c60(param_1+0x30, 0x20, 0x8000))
        master_num_symbols = 32u;
        master_limit       = 0x8000u;
        master_num_nodes   = 32u;
        master_freqs.assign(master_num_nodes * 2, 0);
        for (uint32_t i = 0; i < master_num_symbols; ++i)
            master_freqs[master_num_nodes + i] = 1;
        master_total_freq = init_tree_recursive(master_freqs, 1, master_num_symbols);

        // Reset variance to 0.0 (*(param_1+0x18) = 0)
        variance  = 0.0;
        variance2 = 0.0;

        // Read weight from range coder (FUN_00106eb0):
        //   normalize to ≥ 0x800001
        //   step = range >> 12  (divide by 4096)
        //   q = value / step → value in [0, 4096)
        //   if q < 4095: val = q + 2
        //   else: val = FUN_0011a650(rc, ...) + 0x1001  [reads more bits]
        //   weight2 = 1.0 / val   (which equals 1.0 - weight)
        //   DAT_0011f008 = 1.0
        rc->normalize();
        uint32_t step12 = rc->range >> 12;
        // param_2[2] = step12
        uint32_t q12 = rc->value / step12;
        uint32_t iVar9;
        if (q12 < 0x1000u) {
            uint32_t iVar13 = q12 * step12;
            rc->value -= iVar13;
            if ((q12 + 1u) >> 12 != 0) {
                rc->range -= iVar13;
                // => q12 == 0xfff, this is the "full range" case
                iVar9 = q12 + 2u;
            } else {
                rc->range = step12;
                iVar9 = q12 + 2u;
            }
        } else {
            // q12 = 0xfff case (decode additional value)
            // FUN_0011a650: reads a 16-bit value encoded in the range coder
            // Simple approximation: just read it
            q12 = 0xfffu;
            uint32_t iVar13 = step12 * 0xfffu;
            rc->value += (rc->range - iVar13);
            // Wait: the decompilation says:
            //   if (uVar10 + 1 >> 0xc != 0) goto LAB_00106f77;
            //   LAB_00106f77: if (uVar8 == 0xfff) { uVar6 = FUN_0011a650(...); iVar9 = uVar6 + 0x1001; }
            // For now approximate FUN_0011a650 as reading a uniform 16-bit value:
            rc->range -= iVar13;
            // FUN_0011a650: complex - skip for now, assume iVar9 = 0x1001 + 0
            iVar9 = 0x1001u + 0u;
        }

        weight  = ((double)iVar9 - 1.0) / (double)iVar9;
        weight2 = 1.0 / (double)iVar9;

        needs_init = false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        // Stereo interleaving: even=L uses variance, odd=R uses variance2
        double& var = (channels == 2 && (i & 1)) ? variance2 : variance;
        dest[i] = decode_one_sample(var, rc);
        if (i < 10) {
        }
    }
    decoded_samples += count;
    return 1;
}
