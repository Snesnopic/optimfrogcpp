// minimal lossless encoder: mono 16-bit, pred=1 / ent=1 (fast) / post=1 (identity).
// reuses the decoder's OFR_Predictor (forward) + a fast-entropy encoder mirroring the decoder.
#include "../include/optimfrog_decoder.h"
#include "../include/optimfrog_encoder.h"
#include "optimfrog_tables.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// FUN_00018d80 port (same as decoder's ofr_bitlen)
static int ofr_bitlen(int mn, int mx) {
    uint32_t a = (uint32_t)((mn >> 31) ^ mn);
    uint32_t b = (uint32_t)((mx >> 31) ^ mx);
    if (a == 0 && b == 0) return 1;
    uint32_t m = ((int)b <= (int)a) ? a : b;
    uint32_t u = (m < 0x10000u) ? m : (m >> 16);
    uint32_t bits = (m > 0xffffu) ? 16u : 0u;
    if (u > 0xffu) { u >>= 8; bits += 8; }
    if (u > 0xfu)  { u >>= 4; bits += 4; }
    if (u > 3u)    { u >>= 2; bits += 2; }
    return (int)(bits + 2u + (u > 1u ? 1u : 0u));
}

static uint32_t crc32_table[256];
static void crc32_init() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
}
static uint32_t crc32_calc(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc32_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---- fast entropy encoder (dual of OFR_EntropyDecoder fast path) ----
struct FastCtxE {
    std::vector<uint32_t> freqs;
    uint32_t num_symbols = 0, num_nodes = 0, total_freq = 0, limit = 0;
};
static uint32_t fast_rebuild(std::vector<uint32_t>& f, uint32_t node, uint32_t nn) {
    if (node < nn) { uint32_t l = fast_rebuild(f, node*2, nn), r = fast_rebuild(f, node*2+1, nn); f[node] = l; return l + r; }
    return f[node];
}
static void fast_ctx_init(FastCtxE& c, uint32_t ns, uint32_t limit) {
    c.num_symbols = ns; uint32_t nn = 1; while (nn < ns) nn <<= 1;
    c.num_nodes = nn; c.limit = limit; c.freqs.assign(nn*2, 0u);
    for (uint32_t i = 0; i < ns; i++) c.freqs[nn+i] = 1u;
    c.total_freq = ns; fast_rebuild(c.freqs, 1, nn);
}
static void fast_ctx_halve(FastCtxE& c) {
    c.total_freq = 0;
    for (uint32_t i = 0; i < c.num_symbols; i++) { uint32_t& f = c.freqs[c.num_nodes+i]; f = ((f-1u)>>1)+1u; c.total_freq += f; }
    fast_rebuild(c.freqs, 1, c.num_nodes);
}

struct FastEntropyEncoder {
    std::vector<FastCtxE> ctx;   // shared across channels (as in the decoder)
    double weight = 0, weight2 = 0;
    void init(uint32_t bit_depth, uint32_t w) {
        weight = (double)(w - 1) / (double)w;
        weight2 = 1.0 / (double)w;
        uint32_t num_contexts = bit_depth * 2u;
        uint32_t per = (bit_depth < 4u) ? (1u << bit_depth) : (bit_depth * 8u - 0x10u);
        ctx.assign(num_contexts, FastCtxE());
        for (uint32_t i = 0; i < num_contexts; i++) fast_ctx_init(ctx[i], per, 0x8000u);
    }
    void encode_sample(OFR_RangeEncoder& enc, double& var, int32_t residual) {
        uint32_t value = (residual >= 0) ? ((uint32_t)residual << 1) : (((uint32_t)(-residual) << 1) - 1u);
        uint64_t vbits; std::memcpy(&vbits, &var, 8);
        uint32_t idx = (uint32_t)((vbits >> 52) + 0xfffffc01u);
        FastCtxE& c = ctx[idx];

        uint32_t symbol, group = 0, extra = 0;
        if (value < 8u) { symbol = value; }
        else {
            uint32_t lg = 31u - __builtin_clz(value);   // floor(log2(value))
            group = lg - 3u;
            uint32_t r = value - (1u << (group + 3u));
            uint32_t j = r >> group;
            extra = r & ((1u << group) - 1u);
            symbol = 8u + (group << 3) + j;
        }

        // encode symbol via the same tree walk + increments as fast_decode_sample
        uint32_t step = enc.range / c.total_freq;
        auto& f = c.freqs;
        int L = 0; while ((1u << L) < c.num_nodes) L++;
        uint32_t target = c.num_nodes + symbol;
        uint32_t node = 1, acc = 0;
        for (int i = L - 1; i >= 0; i--) {
            if (((target >> i) & 1u) == 0) { f[node] += 2u; node = 2*node; }
            else { acc += f[node]; node = 2*node + 1; }
        }
        uint32_t leaf_freq = f[node];
        enc.low += (uint64_t)acc * step;
        enc.range = (acc + leaf_freq < c.total_freq) ? (step * leaf_freq) : (enc.range - acc * step);
        enc.renorm();
        f[node] += 2u;
        c.total_freq += 2u;
        if (c.limit <= c.total_freq) fast_ctx_halve(c);

        if (symbol >= 8u) enc.encode_bits(group, extra);

        var = (double)value * (double)value * weight2 + weight2 + var * weight;
    }
};

// ---- slow entropy encoder (dual of OFR_EntropyDecoder::decode_one_sample, FUN_00109ab0) ----
static inline int floor_log2_u(uint32_t x) { return 31 - __builtin_clz(x); }
struct SlowEntropyEncoder {
    OFR_ModelContext master;   // 32-symbol tree (leaves == num_nodes == 32), increment 2
    double weight = 0, weight2 = 0;
    uint32_t bit_depth = 0;
    void init(uint32_t bd, uint32_t w) {
        weight = (double)(w - 1) / (double)w;
        weight2 = 1.0 / (double)w;
        bit_depth = bd;
        master.init(32u, 0x8000u);
    }
    void encode_sample(OFR_RangeEncoder& enc, double& var, int32_t residual) {
        uint32_t raw_val = (residual >= 0) ? ((uint32_t)residual << 1) : (((uint32_t)(-residual) << 1) - 1u);
        uint32_t uVar6 = ((uint32_t)(int64_t)var >> 2) & 0x3fffffffu;
        uint32_t uVar16 = uVar6 + 1u;
        bool escape = false;
        if (uVar16 < 0x4001u) {
            uint32_t q = raw_val / uVar16;
            if (q < 31u) {
                enc.encode_symbol(master, q);
                enc.encode_uniform(uVar16, raw_val - q * uVar16);
            } else {
                enc.encode_symbol(master, 31u);
                uVar16 = uVar16 * 31u; escape = true;
            }
        } else {
            int iVar14 = floor_log2_u(uVar16);
            uint32_t sym2 = raw_val >> iVar14;
            if (sym2 < 31u) {
                enc.encode_symbol(master, sym2);
                enc.encode_bits((uint32_t)iVar14, raw_val & ((1u << iVar14) - 1u));
            } else {
                enc.encode_symbol(master, 31u);
                uVar16 = 0x1fu << iVar14; escape = true;
            }
        }
        if (escape) {
            int new_bits = floor_log2_u(uVar16);
            uint32_t remaining = bit_depth - (uint32_t)new_bits;
            int total_bits = floor_log2_u(raw_val);
            enc.encode_uniform(remaining, (uint32_t)(total_bits - new_bits));
            enc.encode_bits((uint32_t)total_bits, raw_val - (1u << total_bits));
        }
        uint32_t var_raw = (raw_val > 0x3fffffffu) ? 0x3fffffffu : raw_val;
        var = var * weight + (double)var_raw * weight2;
    }
};

static void put32(std::vector<uint8_t>& o, uint32_t v) { o.push_back(v); o.push_back(v>>8); o.push_back(v>>16); o.push_back(v>>24); }
static void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(v); o.push_back(v>>8); }

bool ofr_encode_mono16(const int16_t* samples, uint32_t n, uint32_t samplerate, std::vector<uint8_t>& file) {
    crc32_init();
    int mn = 32767, mx = -32768;
    for (uint32_t i = 0; i < n; i++) { int v = samples[i]; if (v < mn) mn = v; if (v > mx) mx = v; }
    if (n == 0) { mn = 0; mx = 0; }
    int data_bits = ofr_bitlen(mn, mx);
    int sh = (32 - data_bits) & 0x1f;

    // chosen params (written into the bitstream, read back by the decoder)
    // mono predictor is now bit-exact vs the reference (faithful FUN_00014e30+FUN_00012ca0,
    // compiled -fno-fast-math): orders up to ~96 decode losslessly on the reference. order 64
    // ≈ reference preset 4. interval 32 = table index 0.
    int ORDER = 64, INTERVAL = 32, WPRED = 256, WENT = 16, ENT = 1;
    if (getenv("OFR_ORDER")) ORDER = atoi(getenv("OFR_ORDER"));
    if (getenv("OFR_INTERVAL")) INTERVAL = atoi(getenv("OFR_INTERVAL"));
    if (getenv("OFR_WPRED")) WPRED = atoi(getenv("OFR_WPRED"));
    if (getenv("OFR_WENT")) WENT = atoi(getenv("OFR_WENT"));
    if (getenv("OFR_ENT")) ENT = atoi(getenv("OFR_ENT"));   // 1=fast, 2=slow

    int PRED = 1;
    if (getenv("OFR_PRED")) PRED = atoi(getenv("OFR_PRED"));   // 1=LPC, 3=cascade

    OFR_RangeEncoder enc;
    // post=1 init (identity)
    enc.encode_split(16, (uint32_t)mn & 0xffff);
    enc.encode_split(16, (uint32_t)mx & 0xffff);
    enc.encode_bits(1, 0);

    static const int IVT[8] = { 32, 64, 128, 256, 512, 1024, 2048, 0 };
    static const int ODT[9] = { 12, 24, 36, 48, 64, 96, 128, 192, 256 };
    static const int CASC_OT[8] = { 512, 256, 128, 64, 128, 64, 32, 16 };
    std::vector<int32_t> res(n);

    if (PRED == 3) {
        // ---- pred=3 cascade init (exact dual of OFR_PredictorCascadeMono::init) ----
        const uint32_t MAIN_W = 256, FC_W = 256, K = 4, GOLOMB = 0x40;
        const int N_STAGES = 2;
        int main_iv_idx = 0;                 // interval 32
        int main_od_idx = 1;                 // order 24
        int MAIN_IV = IVT[main_iv_idx], MAIN_OD = ODT[main_od_idx];
        std::vector<int> st_oidx = { 1, 6 }; // stage order index -> CASC_OT: 1=256, 6=32
        std::vector<int> st_order = { CASC_OT[st_oidx[0]], CASC_OT[st_oidx[1]] };
        std::vector<int> st_mu10  = { 400, 600 };

        enc.encode_bits(12, MAIN_W - 2);
        enc.encode_bits(3, (uint32_t)main_iv_idx);
        enc.encode_bits(5, (uint32_t)main_od_idx);
        enc.encode_bits(3, (uint32_t)(N_STAGES - 1));
        enc.encode_bits(1, 0);               // decay flag -> 1.0
        enc.encode_bits(12, FC_W - 2);
        enc.encode_bits(3, K);
        enc.encode_bits(1, 0);               // golomb flag -> 0x40
        for (int s = 0; s < N_STAGES; ++s) {
            enc.encode_bits(5, (uint32_t)st_oidx[s]);
            enc.encode_bits(10, (uint32_t)st_mu10[s]);
        }
        // schedule (all-cascade); length nseg+2 with sentinel
        int order_p1 = (int)MAIN_OD + 1;
        int firstcd = std::min(order_p1, (int)n);
        int seg_len = MAIN_IV;
        int nseg = ((int)n + (~firstcd) + seg_len) / seg_len;
        if (nseg < 0) nseg = 0;
        std::vector<uint8_t> sched((size_t)nseg + 2, 1);
        OFR_ModelContext sctx0, sctx1; sctx0.init(2, 0x8000); sctx1.init(2, 0x8000);
        OFR_ModelContext* sctx[2] = { &sctx0, &sctx1 };
        enc.encode_symbol(*sctx[0], sched[0]);
        uint32_t prev = sched[0];
        for (int i = 1; i <= nseg; ++i) { enc.encode_symbol(*sctx[prev], sched[i]); prev = sched[i]; }

        // entropy init
        enc.encode_uniform(4096, (uint32_t)(WENT - 2));

        // setup + forward (cascade machinery is in the -fno-fast-math TU)
        OFR_PredictorCascadeMono cm;
        cm.setup_for_encode(mn, mx, data_bits, (int)n, MAIN_W, MAIN_IV, MAIN_OD, N_STAGES,
                            1.0, FC_W, K, GOLOMB, st_order, st_mu10, sched);
        cm.cascade_init(); cm.sample_counter = 0xac44; cm.need_init = false;
        for (uint32_t i = 0; i < n; i++) {
            if (--cm.sample_counter == 0) cm.sample_counter = 0xac44;
            int out = samples[i];
            if (cm.cc_mode == 0) {
                int p = (int)std::lrint(cm.main_lpc.predict());
                int cp = std::max(mn, std::min(p, mx));
                res[i] = ((out - cp) << sh) >> sh;
                cm.main_lpc.update((double)out); cm.cascade_predict(); cm.cascade_update((double)out);
            } else {
                int p = cm.cascade_predict();
                int cp = std::max(mn, std::min(p, mx));
                res[i] = ((out - cp) << sh) >> sh;
                cm.cascade_update((double)out); cm.main_lpc.update((double)out);
            }
            if (--cm.cc_count == 0) { cm.cc_mode = cm.schedule[cm.sched_idx]; cm.cc_count = cm.seg_len; cm.sched_idx++; }
        }
    } else {
        // ---- pred=1 LPC init ----
        enc.encode_uniform(4096, (uint32_t)(WPRED - 2));
        int iv_idx = -1; for (int i = 0; i < 7; i++) if (IVT[i] == INTERVAL) iv_idx = i;
        if (iv_idx >= 0) enc.encode_bits(3, (uint32_t)iv_idx);
        else { enc.encode_bits(3, 7); enc.encode_split(16, (uint32_t)(INTERVAL - 1)); }
        int od_idx = -1; for (int i = 0; i < 9; i++) if (ODT[i] == ORDER) od_idx = i;
        if (od_idx >= 0) enc.encode_bits(5, (uint32_t)od_idx);
        else { enc.encode_bits(5, 31); enc.encode_bits(8, (uint32_t)(ORDER - 1)); }
        // entropy init
        enc.encode_uniform(4096, (uint32_t)(WENT - 2));

        OFR_Predictor pd;
        pd.init(ORDER, INTERVAL, (double)(WPRED - 1) / (double)WPRED);
        pd.min_val = mn; pd.max_val = mx; pd.shift = sh;
        for (uint32_t i = 0; i < n; i++) {
            int p = (int)std::round(pd.predict());
            int cp = std::max(mn, std::min(p, mx));
            int v = samples[i];
            int err = ((v - cp) << sh) >> sh;
            res[i] = err;
            pd.update((double)v);
        }
    }

    // entropy encode residuals (fast = ent1, slow = ent2)
    if (ENT == 2) {
        SlowEntropyEncoder ent; ent.init((uint32_t)data_bits, (uint32_t)WENT);
        double var = 0.0;   // slow path variance starts at 0.0
        for (uint32_t i = 0; i < n; i++) ent.encode_sample(enc, var, res[i]);
    } else {
        FastEntropyEncoder ent; ent.init((uint32_t)data_bits, (uint32_t)WENT);
        double var = 1.0;
        for (uint32_t i = 0; i < n; i++) ent.encode_sample(enc, var, res[i]);
    }
    enc.flush();

    // ---- assemble file ----
    file.clear();
    // OFR main block
    file.push_back('O'); file.push_back('F'); file.push_back('R'); file.push_back(' ');
    put32(file, 17);                          // main size S = 17
    put32(file, n); put16(file, 0);           // total samples (6 bytes)
    file.push_back(3);                        // sample type SINT16
    file.push_back(0);                        // channel config MONO
    put32(file, samplerate);
    put16(file, 0x2585);                      // encoder ID (informational)
    file.push_back(0x02);                     // compression
    put16(file, 20);                          // minDecoderVersion - 4500
    // HEAD block (empty)
    file.push_back('H'); file.push_back('E'); file.push_back('A'); file.push_back('D');
    put32(file, 0);
    // COMP block
    uint16_t reserved = ((uint16_t)ENT << 11) | ((uint16_t)PRED << 6) | 1u;   // ent, pred, post=1
    std::vector<uint8_t> payload;                       // D-4 bytes (everything after CRC field)
    auto p32 = [&](uint32_t v){ payload.push_back(v); payload.push_back(v>>8); payload.push_back(v>>16); payload.push_back(v>>24); };
    p32(n);                                  // number of samples in block
    payload.push_back(3);                    // sample type
    payload.push_back(0);                    // channel config
    payload.push_back(reserved & 0xff); payload.push_back(reserved >> 8);
    payload.push_back(0x85);                 // encoder ID byte 0 (decoder skips it)
    payload.insert(payload.end(), enc.out.begin(), enc.out.end());  // range stream (out[0] = dummy b0)
    uint32_t D = 4 + (uint32_t)payload.size();          // CRC(4) + payload
    uint32_t crc = crc32_calc(payload.data(), payload.size());
    file.push_back('C'); file.push_back('O'); file.push_back('M'); file.push_back('P');
    put32(file, D);
    put32(file, crc);
    file.insert(file.end(), payload.begin(), payload.end());
    // TAIL block (empty)
    file.push_back('T'); file.push_back('A'); file.push_back('I'); file.push_back('L');
    put32(file, 0);
    return true;
}

// interleaved L/R 16-bit, pred=1 stereo / ent=1 fast stereo / post=1 identity
bool ofr_encode_stereo16(const int16_t* samples, uint32_t frames, uint32_t samplerate, std::vector<uint8_t>& file) {
    crc32_init();
    int mnL = 32767, mxL = -32768, mnR = 32767, mxR = -32768;
    for (uint32_t i = 0; i < frames; i++) {
        int l = samples[2*i], r = samples[2*i+1];
        if (l < mnL) mnL = l; if (l > mxL) mxL = l;
        if (r < mnR) mnR = r; if (r > mxR) mxR = r;
    }
    if (frames == 0) { mnL = mxL = mnR = mxR = 0; }
    int mn = std::min(mnL, mnR), mx = std::max(mxL, mxR);
    int data_bits = ofr_bitlen(mn, mx);
    int sh = (32 - data_bits) & 0x1f;

    static const int IVT[8] = { 32, 64, 128, 256, 512, 1024, 2048, 0 };
    int OD_IDX = 1, IV_IDX = 0, WPRED = 256, WENT = 16, ENT = 1;   // idx1: max_order=24, right_order=8
    if (getenv("OFR_ODIDX")) OD_IDX = atoi(getenv("OFR_ODIDX"));
    if (getenv("OFR_IVIDX")) IV_IDX = atoi(getenv("OFR_IVIDX"));
    if (getenv("OFR_WPRED")) WPRED = atoi(getenv("OFR_WPRED"));
    if (getenv("OFR_WENT")) WENT = atoi(getenv("OFR_WENT"));
    if (getenv("OFR_ENT")) ENT = atoi(getenv("OFR_ENT"));   // 1=fast, 2=slow
    int PRED = 1;
    if (getenv("OFR_PRED")) PRED = atoi(getenv("OFR_PRED"));   // 1=LPC, 3=cascade
    int max_order = DAT_00326220[OD_IDX];
    int right_order = DAT_00326200[OD_IDX];
    int interval = IVT[IV_IDX];
    static const int CASC_OT[8] = { 512, 256, 128, 64, 128, 64, 32, 16 };
    static const int CASC_S2[8] = { 128, 64, 32, 16, 0, 0, 0, 0 };

    OFR_RangeEncoder enc;
    // post=1 init (identity), 2 channels
    enc.encode_split(16, (uint32_t)mnL & 0xffff); enc.encode_split(16, (uint32_t)mxL & 0xffff); enc.encode_bits(1, 0);
    enc.encode_split(16, (uint32_t)mnR & 0xffff); enc.encode_split(16, (uint32_t)mxR & 0xffff); enc.encode_bits(1, 0);
    std::vector<int32_t> res(2 * frames);

    if (PRED == 3) {
        // ---- pred=3 stereo cascade init (dual of OFR_PredictorCascadeStereo::init) ----
        const uint32_t MAIN_W = 256, FC_W = 256, K = 4, GOLOMB = 0x40;
        const int N_STAGES = 2;
        std::vector<int> st_oidx = { 2, 3 };   // size1/size2 from CASC_OT/CASC_S2
        std::vector<int> st_s1 = { CASC_OT[st_oidx[0]], CASC_OT[st_oidx[1]] };
        std::vector<int> st_s2 = { CASC_S2[st_oidx[0]], CASC_S2[st_oidx[1]] };
        std::vector<int> st_mu10 = { 400, 600 };

        enc.encode_bits(12, MAIN_W - 2);
        enc.encode_bits(3, (uint32_t)IV_IDX);
        enc.encode_bits(5, (uint32_t)OD_IDX);
        enc.encode_bits(3, (uint32_t)(N_STAGES - 1));
        enc.encode_bits(1, 0);                 // decay flag -> 1.0
        enc.encode_bits(12, FC_W - 2);
        enc.encode_bits(3, K);
        enc.encode_bits(1, 0);                 // golomb flag -> 0x40
        for (int s = 0; s < N_STAGES; ++s) {
            enc.encode_bits(5, (uint32_t)st_oidx[s]);
            enc.encode_bits(10, (uint32_t)st_mu10[s]);
        }
        // two all-cascade schedules
        int firstcd = std::min((int)max_order - (int)right_order + 1, (int)frames);
        int seg_len = interval;
        int nseg = ((int)frames + (~firstcd) + seg_len) / seg_len;
        if (nseg < 0) nseg = 0;
        std::vector<uint8_t> schL((size_t)nseg + 2, 1), schR((size_t)nseg + 2, 1);
        OFR_ModelContext cl0, cl1, cr0, cr1;
        cl0.init(2, 0x8000); cl1.init(2, 0x8000); cr0.init(2, 0x8000); cr1.init(2, 0x8000);
        OFR_ModelContext* cl[2] = { &cl0, &cl1 }; OFR_ModelContext* cr[2] = { &cr0, &cr1 };
        enc.encode_symbol(*cl[0], schL[0]); uint32_t pL = schL[0];
        enc.encode_symbol(*cr[0], schR[0]); uint32_t pR = schR[0];
        for (int i = 1; i <= nseg; ++i) {
            enc.encode_symbol(*cl[pL], schL[i]); pL = schL[i];
            enc.encode_symbol(*cr[pR], schR[i]); pR = schR[i];
        }
        // entropy init
        enc.encode_uniform(4096, (uint32_t)(WENT - 2));

        OFR_PredictorCascadeStereo cs;
        cs.setup_for_encode(mnL, mxL, mnR, mxR, data_bits, (int)(2 * frames),
                            MAIN_W, (uint32_t)interval, (uint32_t)max_order, (uint32_t)right_order,
                            N_STAGES, 1.0, FC_W, K, GOLOMB, st_s1, st_s2, st_mu10, schL, schR);
        cs.cascade_init(); cs.sample_counter = 0xac44; cs.need_init = false;
        for (uint32_t f = 0; f < frames; f++) {
            if (--cs.sample_counter == 0) cs.sample_counter = 0xac44;
            int L = samples[2*f], R = samples[2*f+1];
            if (cs.mode_L == 0) {
                int p = (int)std::lrint(cs.main.predictLeft());
                int cp = std::max(mnL, std::min(p, mxL));
                res[2*f] = ((L - cp) << sh) >> sh;
                cs.main.updateLeft((double)L);
                cs.sub_predict(cs.casL, cs.ringsA, cs.ringsB);
                cs.sub_update(cs.casL, cs.ringsA, cs.ringsB, (double)L);
            } else {
                int p = cs.sub_predict(cs.casL, cs.ringsA, cs.ringsB);
                int cp = std::max(mnL, std::min(p, mxL));
                res[2*f] = ((L - cp) << sh) >> sh;
                cs.sub_update(cs.casL, cs.ringsA, cs.ringsB, (double)L);
                cs.main.updateLeft((double)L);
            }
            if (cs.mode_R == 0) {
                int p = (int)std::lrint(cs.main.predictRight());
                int cp = std::max(mnR, std::min(p, mxR));
                res[2*f+1] = ((R - cp) << sh) >> sh;
                cs.main.updateRight((double)R);
                cs.sub_predict(cs.casR, cs.ringsB, cs.ringsA);
                cs.sub_update(cs.casR, cs.ringsB, cs.ringsA, (double)R);
            } else {
                int p = cs.sub_predict(cs.casR, cs.ringsB, cs.ringsA);
                int cp = std::max(mnR, std::min(p, mxR));
                res[2*f+1] = ((R - cp) << sh) >> sh;
                cs.sub_update(cs.casR, cs.ringsB, cs.ringsA, (double)R);
                cs.main.updateRight((double)R);
            }
            if (--cs.cc_count == 0) { cs.mode_L = cs.schedL[cs.sched_idx]; cs.mode_R = cs.schedR[cs.sched_idx]; cs.cc_count = cs.seg_len; cs.sched_idx++; }
        }
    } else {
        // ---- pred=1 stereo ----
        enc.encode_bits(12, (uint32_t)(WPRED - 2));
        enc.encode_bits(3, (uint32_t)IV_IDX);
        enc.encode_bits(5, (uint32_t)OD_IDX);
        enc.encode_uniform(4096, (uint32_t)(WENT - 2));

        OFR_PredictorStereo_Inner inner;
        inner.init((double)(WPRED - 1) / (double)WPRED, max_order, max_order - right_order, (uint32_t)interval);
        for (uint32_t f = 0; f < frames; f++) {
            int L = samples[2*f], R = samples[2*f+1];
            int prL = (int)std::lrint(inner.predictLeft());
            int cl = std::max(mnL, std::min(prL, mxL));
            res[2*f] = ((L - cl) << sh) >> sh;
            inner.updateLeft((double)L);
            int prR = (int)std::lrint(inner.predictRight());
            int cr = std::max(mnR, std::min(prR, mxR));
            res[2*f+1] = ((R - cr) << sh) >> sh;
            inner.updateRight((double)R);
        }
    }

    // entropy encode (interleaved L/R, two variances)
    if (ENT == 2) {
        SlowEntropyEncoder ent; ent.init((uint32_t)data_bits, (uint32_t)WENT);
        double var_L = 0.0, var_R = 0.0;
        for (uint32_t i = 0; i < 2 * frames; i += 2) {
            ent.encode_sample(enc, var_L, res[i]);
            ent.encode_sample(enc, var_R, res[i+1]);
        }
    } else {
        FastEntropyEncoder ent; ent.init((uint32_t)data_bits, (uint32_t)WENT);   // shared contexts
        double var_L = 1.0, var_R = 1.0;
        for (uint32_t i = 0; i < 2 * frames; i += 2) {
            ent.encode_sample(enc, var_L, res[i]);
            ent.encode_sample(enc, var_R, res[i+1]);
        }
    }
    enc.flush();

    uint32_t values = 2 * frames;
    file.clear();
    file.push_back('O'); file.push_back('F'); file.push_back('R'); file.push_back(' ');
    put32(file, 17);
    put32(file, values); put16(file, 0);      // total samples (values)
    file.push_back(3);                         // SINT16
    file.push_back(1);                         // STEREO_LR
    put32(file, samplerate);
    put16(file, 0x2585); file.push_back(0x02); put16(file, 20);
    file.push_back('H'); file.push_back('E'); file.push_back('A'); file.push_back('D');
    put32(file, 0);
    uint16_t reserved = ((uint16_t)ENT << 11) | ((uint16_t)PRED << 6) | 1u;
    std::vector<uint8_t> payload;
    auto p32 = [&](uint32_t v){ payload.push_back(v); payload.push_back(v>>8); payload.push_back(v>>16); payload.push_back(v>>24); };
    p32(values);
    payload.push_back(3); payload.push_back(1);
    payload.push_back(reserved & 0xff); payload.push_back(reserved >> 8);
    payload.push_back(0x85);
    payload.insert(payload.end(), enc.out.begin(), enc.out.end());
    uint32_t D = 4 + (uint32_t)payload.size();
    uint32_t crc = crc32_calc(payload.data(), payload.size());
    file.push_back('C'); file.push_back('O'); file.push_back('M'); file.push_back('P');
    put32(file, D); put32(file, crc);
    file.insert(file.end(), payload.begin(), payload.end());
    file.push_back('T'); file.push_back('A'); file.push_back('I'); file.push_back('L');
    put32(file, 0);
    return true;
}

#ifdef OFRENC_MAIN
int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s in.raw out.ofr samplerate [channels=1]\n", argv[0]); return 2; }
    int ch = (argc >= 5) ? atoi(argv[4]) : 1;
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("in"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<int16_t> s(sz / 2);
    fread(s.data(), 2, s.size(), f); fclose(f);
    std::vector<uint8_t> out;
    if (ch == 2) ofr_encode_stereo16(s.data(), (uint32_t)(s.size() / 2), (uint32_t)atoi(argv[3]), out);
    else ofr_encode_mono16(s.data(), (uint32_t)s.size(), (uint32_t)atoi(argv[3]), out);
    FILE* g = fopen(argv[2], "wb"); fwrite(out.data(), 1, out.size(), g); fclose(g);
    fprintf(stderr, "wrote %zu bytes (ch=%d)\n", out.size(), ch);
    return 0;
}
#endif
