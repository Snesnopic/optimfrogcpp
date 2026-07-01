// Entropy decoder – rewritten from Ghidra decompilation of FUN_00109ab0.
// See comments in implementation_plan.md for the full analysis.

#include "optimfrog_decoder.h"
#include <cstring>
#include <iostream>
#include <cassert>
#include <cstdio>
#include <cstdlib>

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

void OFR_EntropyDecoder::init(uint32_t depth, uint32_t ch, uint32_t entropy_type, bool fast_stereo) {
    this->bit_depth = depth;
    this->channels = ch;
    this->type = entropy_type;
    this->is_fast_stereo = fast_stereo;
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

// FUN_00004220 — called once per block.
// Reads 12-bit weight param from RC (via read_12bit_value which matches FUN_00004220 exactly).
// Sets needs_init=true so decode_block will reset the master context before the first sample.
void OFR_EntropyDecoder::init_from_bitstream(OFR_RangeCoder* rc) {
    uint32_t w = rc->read_12bit_value();
    weight  = (double)(w - 1) / (double)w;
    weight2 = 1.0 - weight;   // binary derives weight2 from weight (1-ULP off vs 1/w)
    decoded_samples = 0;
    needs_init = true;
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
    // Line 687: update variance in-place. Disassembly (0x5e1d-0x5e48) shows `movl %ebp,%eax`
    // (zero-extends into RAX) followed by `cvtsi2sd %rax,%xmm1` -- a 64-BIT signed convert of
    // a zero-extended 32-bit value, i.e. raw_val is always treated as non-negative, never as
    // a signed int32 and never clamped. Our old `raw_val > 0x3fffffff ? 0x3fffffff : raw_val`
    // clamp was a wrong invention: invisible at 16/24-bit (raw_val never approaches 0x3fffffff
    // there) but wrong for full-range 32-bit content, where it under-counted the variance.
    var = var * weight + (double)(uint64_t)raw_val * weight2;

    // Lines 686, 688-691: zigzag decode
    // uVar6 = raw_val >> 1;  if (raw_val & 1) uVar6 = ~uVar6;
    uint32_t out = raw_val >> 1u;
    if (raw_val & 1u) out = ~out;
    return (int32_t)out;
}

// ============================================================
// fast stereo entropy (FUN_00004710 / FUN_00005ef0)
// ============================================================

// rebuild one context tree: internal[node] = sum of left subtree (FUN_00002ea0)
static uint32_t fast_rebuild(std::vector<uint32_t>& f, uint32_t node, uint32_t num_nodes) {
    if (node < num_nodes) {
        uint32_t l = fast_rebuild(f, node * 2, num_nodes);
        uint32_t r = fast_rebuild(f, node * 2 + 1, num_nodes);
        f[node] = l;
        return l + r;
    }
    return f[node];
}

// FUN_00002bd0: init one context with num_symbols leaves
static void fast_ctx_init(OFR_EntropyDecoder::FastCtx& c, uint32_t num_symbols, uint32_t limit) {
    c.num_symbols = num_symbols;
    uint32_t nn = 1;
    while (nn < num_symbols) nn <<= 1;
    c.num_nodes = nn;
    c.limit = limit;
    c.freqs.assign(nn * 2, 0u);
    for (uint32_t i = 0; i < num_symbols; ++i) c.freqs[nn + i] = 1u;
    c.total_freq = num_symbols;
    fast_rebuild(c.freqs, 1, nn);
}

// FUN_00002df0: halve leaf frequencies and rebuild
static void fast_ctx_halve(OFR_EntropyDecoder::FastCtx& c) {
    c.total_freq = 0;
    for (uint32_t i = 0; i < c.num_symbols; ++i) {
        uint32_t& f = c.freqs[c.num_nodes + i];
        f = ((f - 1u) >> 1) + 1u;
        c.total_freq += f;
    }
    fast_rebuild(c.freqs, 1, c.num_nodes);
}

// FUN_00005ef0: decode one sample using the variance-indexed context
int32_t OFR_EntropyDecoder::fast_decode_sample(double& var, OFR_RangeCoder* rc) {
    uint64_t vbits;
    std::memcpy(&vbits, &var, 8);
    uint32_t idx = (uint32_t)((vbits >> 52) + 0xfffffc01u);   // unbiased exponent
    FastCtx& c = fast_contexts[idx];
    auto& f = c.freqs;

    rc->normalize();
    uint32_t step = rc->range / c.total_freq;
    uint32_t v    = rc->value / step;
    uint32_t bound = (v < c.total_freq) ? v : c.total_freq - 1u;

    uint32_t node = 1, acc = 0;
    do {
        uint32_t nodeval = f[node];
        uint32_t s = nodeval + acc;
        uint32_t child;
        if (bound < s) {
            f[node] = nodeval + 2u;
            child = node;
        } else {
            child = node + 1u;
            acc = s;
        }
        node = node + child;
    } while (node < c.num_nodes);

    uint32_t leaf_freq = f[node];
    uint32_t accstep   = step * acc;
    rc->value -= accstep;
    if (acc + leaf_freq < c.total_freq) rc->range = step * leaf_freq;
    else                                rc->range -= accstep;
    f[node] += 2u;
    c.total_freq += 2u;
    if (c.limit <= c.total_freq) fast_ctx_halve(c);

    uint32_t symbol = node - c.num_nodes;
    uint32_t value  = symbol;
    if (symbol >= 8u) {
        uint32_t group = (symbol - 8u) >> 3;
        uint32_t extra = rc->read_uniform_bits(group);
        value = (((symbol - 8u) & 7u) << group) + (1u << (group + 3u)) + extra;
    }

    var = (double)value * (double)value * weight2 + weight2 + var * weight;

    return (int32_t)((value & 1u) ? ~(value >> 1) : (value >> 1));
}

// ============================================================
// decode_block
// ============================================================
int32_t OFR_EntropyDecoder::decode_block(int32_t* dest, uint32_t count, OFR_RangeCoder* rc) {
    if (type == 1) {   // fast entropy (FUN_00004710), 1 or 2 channels
        if (needs_init) {
            uint32_t num_contexts = bit_depth * 2u;
            uint32_t per_symbols = (bit_depth < 4u) ? (1u << bit_depth) : (bit_depth * 8u - 0x10u);
            fast_contexts.assign(num_contexts, FastCtx());
            for (uint32_t i = 0; i < num_contexts; ++i)
                fast_ctx_init(fast_contexts[i], per_symbols, 0x8000u);
            fast_var_L = 1.0;
            fast_var_R = 1.0;
            needs_init = false;
        }
        if (channels == 2) {
            for (uint32_t i = 0; i < count; i += 2) {
                dest[i]     = fast_decode_sample(fast_var_L, rc);
                dest[i + 1] = fast_decode_sample(fast_var_R, rc);
            }
        } else {
            for (uint32_t i = 0; i < count; ++i)
                dest[i] = fast_decode_sample(fast_var_L, rc);
        }
        decoded_samples += count;
        return 1;
    }
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


        needs_init = false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        // Stereo interleaving: even=L uses variance, odd=R uses variance2
        double& var = (channels == 2 && (i & 1)) ? variance2 : variance;
        dest[i] = decode_one_sample(var, rc);
    }
    decoded_samples += count;
    return 1;
}
