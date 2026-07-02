// IEEE Float (.off, "OFRX") encoder: the dual of optimfrog_decoder_float.cpp's
// reconstruct_float_block. Transforms each float32 sample into an integer for the existing
// lossless pred1/ent2/post1 encoder (reused as-is, unmodified) plus a "DETA" sub-block carrying
// whatever that transform couldn't capture (escaped samples, residual mantissa bits).
//
// Chosen per-block parameters (sigShift is fixed at 0, i.e. no GCD-based bit-shaving
// optimization) don't need to match what the reference encoder would pick -- only be valid and
// self-consistent, same philosophy as the rest of this project's encoder (see
// optimfrog_encoder.cpp). Verified interoperable with the real off binary's decoder.
#include "../../include/optimfrog_encoder.h"
#include <cstring>
#include <algorithm>

static uint32_t crc32_table_f[256];
static void crc32_init_f() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table_f[i] = c;
    }
}
static uint32_t crc32_calc_f(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc32_table_f[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void put32f(std::vector<uint8_t>& o, uint32_t v) { o.push_back(v); o.push_back(v>>8); o.push_back(v>>16); o.push_back(v>>24); }

struct FloatBlockStats { int32_t minExp, maxExp; };

// per-block signed-exponent min/max, excluding zero/inf-nan/extreme-denormal-floor samples --
// mirrors the exclusions in the reference's own per-block stats scan.
static FloatBlockStats compute_block_stats(const float* samples, uint32_t n) {
    int32_t minExp = 0x7fffffff, maxExp = -0x7fffffff - 1;
    bool any = false;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bits; memcpy(&bits, &samples[i], 4);
        uint32_t biased = (bits >> 23) & 0xff;
        uint32_t mant = bits & 0x7fffffu;
        if (biased == 0xff) continue; // inf/nan
        int32_t e;
        if (biased == 0) {
            if (mant == 0) continue; // true zero
            uint32_t m = mant; e = -126;
            while (!(m & 0x800000u)) { m <<= 1; e--; }
            if (e == -150) continue; // extreme denormal floor
        } else {
            e = (int32_t)biased - 127;
        }
        if (e < minExp) minExp = e;
        if (e > maxExp) maxExp = e;
        any = true;
    }
    if (!any) { minExp = 0; maxExp = 0; }
    return {minExp, maxExp};
}

// dual of the decoder's per-sample reconstruction: writes escape/residual data into deta_enc,
// returns the OUTPUT integer fed to the ordinary pred1/ent2/post1 encoder.
static int32_t float_to_output(float f, int32_t iVar3, int32_t local_60, int32_t local_74, int32_t sigShift,
                                OFR_RangeEncoder& deta_enc, OFR_ModelContext& ctx2, OFR_ModelContext& ctx279) {
    uint32_t bits; memcpy(&bits, &f, 4);
    uint32_t biased_exp = (bits >> 23) & 0xff;
    uint32_t mant = bits & 0x7fffffu;
    bool sign = (bits >> 31) != 0;

    if (biased_exp == 0xff) { // inf/nan
        deta_enc.encode_symbol(ctx279, 0x116);
        uint32_t packed = (mant << 1) | (sign ? 1u : 0u);
        deta_enc.encode_bits(12, packed & 0xfffu);
        deta_enc.encode_bits(12, packed >> 12);
        return 0;
    }

    int32_t exp_i = 0;
    uint32_t frac = 0;
    bool is_true_zero = false;

    if (biased_exp == 0) {
        if (mant == 0) {
            is_true_zero = true;
        } else {
            uint32_t m = mant; int32_t e = -126;
            while (!(m & 0x800000u)) { m <<= 1; e--; }
            if (e == -150) { is_true_zero = true; }
            else { exp_i = e; frac = m & 0x7fffffu; }
        }
    } else {
        exp_i = (int32_t)biased_exp - 127;
        frac = mant;
    }

    if (is_true_zero) {
        deta_enc.encode_symbol(ctx279, 0);
        deta_enc.encode_symbol(ctx2, sign ? 1u : 0u);
        return 0;
    }

    int32_t iVar5 = exp_i + iVar3 - local_74;
    int32_t bits_avail = exp_i - local_60;
    int32_t cap = 23 - sigShift;
    if (cap < bits_avail) bits_avail = cap;
    if (bits_avail < 0) bits_avail = 0;

    if (iVar5 >= 0) {
        // fast path
        uint32_t mantissa_full = 0x800000u | frac;
        int32_t mag = (int32_t)(mantissa_full >> (23 - iVar5));
        int32_t out = sign ? -mag : mag;
        int32_t residual_bits = bits_avail - iVar5;
        if (residual_bits > 0) {
            uint32_t extra = (frac >> (23 - bits_avail)) & ((1u << residual_bits) - 1u);
            deta_enc.encode_split((uint32_t)residual_bits, extra);
        }
        return out;
    } else {
        // escape
        deta_enc.encode_symbol(ctx279, (uint32_t)(exp_i + 150));
        deta_enc.encode_uniform(2, sign ? 1u : 0u);
        if (bits_avail > 0) {
            uint32_t topbits = frac >> (23 - bits_avail);
            deta_enc.encode_split((uint32_t)bits_avail, topbits);
        }
        return 0;
    }
}

// Encodes samples[0..n) (mono) or interleaved L/R (stereo, n = frames*2) into the DETA byte
// stream + the OUTPUT integer array fed to the existing lossless encoder. Shared by both
// ofr_encode_mono_float/ofr_encode_stereo_float since the DETA header/contexts aren't
// per-channel (matches how the decoder processes the interleaved array uniformly).
static void build_deta_and_output(const float* samples, uint32_t n, std::vector<uint8_t>& deta,
                                   std::vector<int32_t>& output) {
    FloatBlockStats st = compute_block_stats(samples, n);
    int32_t sigShift = 0;
    int32_t iVar3 = std::min(23 - sigShift, st.maxExp - st.minExp);
    int32_t local_60 = st.minExp - (23 - sigShift);
    if (local_60 < -150) local_60 = -150; // stay within the 9-bit (val+150 in [0,511]) header field range
    int32_t local_74 = st.maxExp;

    OFR_RangeEncoder deta_enc;
    deta_enc.encode_uniform(256, (uint32_t)iVar3);
    deta_enc.encode_uniform(256, (uint32_t)sigShift);
    deta_enc.encode_uniform(512, (uint32_t)(local_60 + 150));
    deta_enc.encode_uniform(512, (uint32_t)(local_74 + 150));

    OFR_ModelContext ctx2, ctx279;
    ctx2.init(2, 0x8000);
    ctx279.init(0x117, 0x8000);

    output.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        output[i] = float_to_output(samples[i], iVar3, local_60, local_74, sigShift, deta_enc, ctx2, ctx279);
    }
    deta_enc.flush();
    // DETA's range coder init (FUN_10002bef0/FUN_10002b5a0 in off) writes/checks an explicit
    // '@' (0x40) marker byte as the stream's first byte -- unlike the main COMP range coder,
    // where the first emitted byte really is an arbitrary, ignored dummy. Force it here.
    deta_enc.out[0] = 0x40;

    deta.clear();
    deta.push_back('D'); deta.push_back('E'); deta.push_back('T'); deta.push_back('A');
    uint32_t payload_len = (uint32_t)deta_enc.out.size();
    put32f(deta, payload_len + 4);
    uint32_t crc = crc32_calc_f(deta_enc.out.data(), deta_enc.out.size());
    put32f(deta, crc);
    deta.insert(deta.end(), deta_enc.out.begin(), deta_enc.out.end());
}

// Patches a plain "OFR "-magic file produced by the existing lossless encoder into an
// "OFRX" float file: swaps the magic + both sample_type bytes (main header and COMP payload,
// SINT32=7 -> FLOAT32_1=8), splices the DETA sub-block right after COMP (before TAIL), and
// substitutes the real head/tail bytes for the empty placeholders used when calling the
// lossless encoder.
static bool splice_float_file(std::vector<uint8_t>& inner, const std::vector<uint8_t>& deta,
                               const std::vector<uint8_t>& head, const std::vector<uint8_t>& tail,
                               std::vector<uint8_t>& file) {
    if (inner.size() < 15) return false;
    inner[3] = 'X';
    inner[14] = 8; // main header sample_type

    size_t comp_pos = std::string::npos;
    for (size_t i = 0; i + 4 <= inner.size(); i++) {
        if (inner[i] == 'C' && inner[i+1] == 'O' && inner[i+2] == 'M' && inner[i+3] == 'P') { comp_pos = i; break; }
    }
    if (comp_pos == std::string::npos) return false;
    uint32_t D; memcpy(&D, &inner[comp_pos + 4], 4);
    size_t payload_start = comp_pos + 12; // COMP(4) + D(4) + CRC(4)
    size_t sampletype_off = payload_start + 4; // numSamples(4) then sampleType(1)
    if (sampletype_off >= inner.size()) return false;
    inner[sampletype_off] = 8; // COMP payload's own sample_type byte

    // patching that byte invalidates COMP's own CRC32 (covers everything from payload_start
    // onward, D-4 bytes) -- recompute and overwrite it in place.
    uint32_t new_crc = crc32_calc_f(&inner[payload_start], D - 4);
    memcpy(&inner[comp_pos + 8], &new_crc, 4);

    size_t tail_pos = std::string::npos;
    for (size_t i = comp_pos; i + 4 <= inner.size(); i++) {
        if (inner[i] == 'T' && inner[i+1] == 'A' && inner[i+2] == 'I' && inner[i+3] == 'L') { tail_pos = i; break; }
    }
    if (tail_pos == std::string::npos) return false;

    size_t head_pos = std::string::npos;
    for (size_t i = 0; i + 4 <= comp_pos; i++) {
        if (inner[i] == 'H' && inner[i+1] == 'E' && inner[i+2] == 'A' && inner[i+3] == 'D') { head_pos = i; break; }
    }
    if (head_pos == std::string::npos) return false;
    uint32_t headSize; memcpy(&headSize, &inner[head_pos + 4], 4);

    file.clear();
    file.insert(file.end(), inner.begin(), inner.begin() + head_pos + 8); // up through HEAD's size field (0)
    file.insert(file.end(), head.begin(), head.end());
    file.insert(file.end(), inner.begin() + head_pos + 8 + headSize, inner.begin() + tail_pos); // COMP block
    file.insert(file.end(), deta.begin(), deta.end());
    file.push_back('T'); file.push_back('A'); file.push_back('I'); file.push_back('L');
    put32f(file, (uint32_t)tail.size());
    file.insert(file.end(), tail.begin(), tail.end());
    return true;
}

bool ofr_encode_mono_float(const float* samples, uint32_t n, uint32_t samplerate, std::vector<uint8_t>& file,
                            const std::vector<uint8_t>& head, const std::vector<uint8_t>& tail) {
    crc32_init_f();
    std::vector<uint8_t> deta;
    std::vector<int32_t> output;
    build_deta_and_output(samples, n, deta, output);

    std::vector<uint8_t> inner;
    if (!ofr_encode_mono(output.data(), n, samplerate, 32, inner, {}, {})) return false;
    return splice_float_file(inner, deta, head, tail, file);
}

bool ofr_encode_stereo_float(const float* samples, uint32_t frames, uint32_t samplerate, std::vector<uint8_t>& file,
                              const std::vector<uint8_t>& head, const std::vector<uint8_t>& tail) {
    crc32_init_f();
    std::vector<uint8_t> deta;
    std::vector<int32_t> output;
    build_deta_and_output(samples, (uint32_t)frames * 2, deta, output);

    std::vector<uint8_t> inner;
    if (!ofr_encode_stereo(output.data(), frames, samplerate, 32, inner, {}, {})) return false;

    // ofr_encode_stereo's channelConfig byte differs from mono's (1 vs 0); splice_float_file
    // patches the shared magic/sample_type/DETA-insertion logic identically for both.
    return splice_float_file(inner, deta, head, tail, file);
}
