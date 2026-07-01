// entropy_type=3 — "acm" advanced context-modeling entropy (preset max). All integer.
// Full RE map in doc/ent3_analysis.md. Cores: init FUN_00004c00, per-sample FUN_00006100.

#include "../../include/optimfrog_decoder.h"
#include <cstring>

namespace {
using Ctx = OFR_EntropyAcm::Ctx;

uint32_t ctx_rebuild(std::vector<uint32_t>& f, uint32_t node, uint32_t nn) {
    if (node < nn) {
        uint32_t l = ctx_rebuild(f, node*2, nn);
        uint32_t r = ctx_rebuild(f, node*2+1, nn);
        f[node] = l; return l + r;
    }
    return f[node];
}
void ctx_init(Ctx& c, uint32_t num_symbols, uint32_t limit) {
    c.num_symbols = num_symbols;
    uint32_t nn = 1; while (nn < num_symbols) nn <<= 1;
    c.num_nodes = nn; c.limit = limit;
    c.freqs.assign(nn*2, 0u);
    for (uint32_t i = 0; i < num_symbols; ++i) c.freqs[nn+i] = 1u;
    c.total_freq = num_symbols;
    ctx_rebuild(c.freqs, 1, nn);
}
void ctx_halve(Ctx& c) {
    c.total_freq = 0;
    for (uint32_t i = 0; i < c.num_symbols; ++i) {
        uint32_t& f = c.freqs[c.num_nodes+i];
        f = ((f-1u)>>1)+1u; c.total_freq += f;
    }
    ctx_rebuild(c.freqs, 1, c.num_nodes);
}
uint32_t ctx_decode(Ctx& c, OFR_RangeCoder* rc) {
    rc->normalize();
    uint32_t step = rc->range / c.total_freq;
    uint32_t v = rc->value / step;
    uint32_t bound = (v < c.total_freq) ? v : c.total_freq - 1u;
    auto& f = c.freqs;
    uint32_t node = 1, acc = 0;
    do {
        uint32_t nv = f[node]; uint32_t s = nv + acc; uint32_t child;
        if (bound < s) { f[node] = nv + 2u; child = node; }
        else { child = node + 1u; acc = s; }
        node = node + child;
    } while (node < c.num_nodes);
    uint32_t leaf = f[node]; uint32_t accstep = step * acc;
    rc->value -= accstep;
    if (acc + leaf < c.total_freq) rc->range = step * leaf; else rc->range -= accstep;
    f[node] += 2u; c.total_freq += 2u;
    if (c.limit <= c.total_freq) ctx_halve(c);
    return node - c.num_nodes;
}
// uniform decode of N symbols (matches range>>k / range/N cap N-1)
uint32_t uni(OFR_RangeCoder* rc, uint32_t N) {
    rc->normalize();
    uint32_t step = rc->range / N;
    uint32_t q = rc->value / step;
    uint32_t qc = (q < N) ? q : N - 1u;
    rc->value -= qc * step;
    if (qc + 1u < N) rc->range = step; else rc->range -= qc * step;
    return qc;
}
// bit-length ladder on (mag>>16), as in FUN_00006100
uint32_t mag_exp(int64_t mag) {
    uint32_t u17 = (uint32_t)((uint64_t)mag >> 16);
    uint32_t u19 = (uint32_t)((uint64_t)mag >> 32) & 0xffffu;
    if (u17 < 0x10000u) u19 = u17;
    uint32_t b = (u17 > 0xffffu) ? 16u : 0u;
    if (u19 > 0xffu) { u19 >>= 8; b |= 8u; }
    if (u19 > 0xfu)  { u19 >>= 4; b += 4u; }
    if (u19 > 3u)    { u19 >>= 2; b += 2u; }
    return (u19 > 1u ? 1u : 0u) + b;
}
uint32_t int_bitlen(uint32_t v) {  // (1<x)+ladder, matches escape's bit count of v
    uint32_t u19 = (v >> 16) ? (v >> 16) : v;
    uint32_t b = (v > 0xffffu) ? 16u : 0u;
    if (u19 > 0xffu) { u19 >>= 8; b |= 8u; }
    if (u19 > 0xfu)  { u19 >>= 4; b += 4u; }
    if (u19 > 3u)    { u19 >>= 2; b += 2u; }
    return (u19 > 1u ? 1u : 0u) + b;
}
} // namespace

void OFR_EntropyAcm::init(OFR_RangeCoder* rc, uint32_t bd, uint32_t ch, uint32_t total) {
    bit_depth = bd; channels = ch; total_samples = total;
    chans.assign(ch, Chan());
    for (uint32_t cidx = 0; cidx < ch; ++cidx) {
        Chan& c = chans[cidx];
        c.msb_flag = uni(rc, 2) != 0;                     // +0x08
        uint32_t ridx = uni(rc, 16);                      // 4-bit
        uint32_t reset = 1u << ((ridx >> 1) + 6);
        if (ridx & 1) reset += reset >> 1;
        c.reset = reset;                                  // +0x0c

        // template
        bool tmpl = uni(rc, 2) != 0;
        uint32_t t_transform=0, t_scale=0, t_k=0, t_nsym=0;
        if (tmpl) {
            t_transform = uni(rc, 2) + 1;
            t_scale = 1u << (uni(rc, 8) + 12);
            t_k = rc->read_uniform_bits(8) + 1;
            t_nsym = uni(rc, 0x80) + 2;
        }

        c.vmap.assign(bit_depth, 0);
        // worst case: one new context per exponent
        c.vctx.assign(bit_depth, Ctx());
        c.k.assign(bit_depth, 0);
        c.nsym.assign(bit_depth, 0);
        c.transform.assign(bit_depth, 0);
        c.scale.assign(bit_depth, 0);

        int cur = -1;
        for (uint32_t e = 0; e < bit_depth; ++e) {
            uint32_t bit = uni(rc, 2);
            if (bit != 0) {
                cur++;
                uint32_t transform, scale, k, nsym;
                if (tmpl) {
                    transform = t_transform; scale = t_scale; k = t_k; nsym = t_nsym;
                } else {
                    transform = uni(rc, 2) + 1;
                    scale = 1u << (uni(rc, 8) + 12);
                    k = rc->read_uniform_bits(8) + 1;
                    nsym = uni(rc, 0x80) + 2;
                }
                c.transform[cur] = transform; c.scale[cur] = scale;
                c.k[cur] = k; c.nsym[cur] = nsym;
                ctx_init(c.vctx[cur], nsym, scale);
            }
            c.vmap[e] = cur;
        }

        for (int i = 0; i < 8; ++i) c.hist[i] = 0;
        c.bitlen.assign(9, Ctx());
        for (int i = 0; i < 9; ++i) ctx_init(c.bitlen[i], 9, 0x8000);
        c.countdown = 0;
        c.state = 0;
    }
    needs_init = false;
}

uint32_t OFR_EntropyAcm::decode_sample(Chan& c, OFR_RangeCoder* rc) {
    uint32_t state = c.state;
    if (c.countdown == 0) {
        uint32_t sym = ctx_decode(c.bitlen[state], rc);
        c.state = sym;
        c.countdown = c.reset;
        if (sym == 8) {
            for (int i = 0; i < 8; ++i) c.hist[i] = 0;
            c.countdown--;
            return 0;
        }
        c.countdown--;
        state = sym;
    } else {
        c.countdown--;
        if (state == 8) return 0;
    }

    int64_t mag = c.hist[state];
    if (c.msb_flag && state != 0 && c.hist[state - 1] > mag) mag = c.hist[state - 1];
    uint32_t e = mag_exp(mag);
    int ctx = c.vmap[e];
    uint32_t k = c.k[ctx];
    uint32_t pred = (uint32_t)(((uint64_t)k * (uint64_t)mag) >> 24) + 1u;

    uint32_t sym2 = ctx_decode(c.vctx[ctx], rc);
    uint32_t value;
    if (sym2 == c.nsym[ctx] - 1u) {
        // escape
        uint32_t v = sym2 * pred;
        uint32_t bits = int_bitlen(v);
        // guard mirrors the ent=2 escape check: bits must stay below bit_depth or
        // `bit_depth - bits` underflows (uint32_t) and the range coder locks up.
        // Real audio never hits this (bit_depth is always comfortably larger than
        // the escape bit count); only pathological near-constant tiny-amplitude
        // synthetic signals with a very small derived bit_depth can.
        if (bits >= bit_depth) {
            value = 0;
        } else {
            uint32_t rem = bit_depth - bits;
            uint32_t idx = uni(rc, rem);
            uint32_t low = rc->read_uniform_bits(idx + bits);
            value = (1u << (idx + bits)) + low;
        }
    } else {
        uint32_t residual;
        if (pred < 0x4001u) {
            residual = uni(rc, pred);
        } else {
            uint32_t u19 = (pred - 1u + 0x4000u) >> 14;
            uint32_t u18 = (pred - 1u + u19) / u19;
            uint32_t high = uni(rc, u18);
            uint32_t low = uni(rc, u19);
            residual = high * u19 + low;
        }
        value = sym2 * pred + residual;
    }

    int64_t lv = (int64_t)value << 16;
    for (int i = 0; i < 8; ++i) c.hist[i] += (lv - c.hist[i]) >> (3 + i);

    return ((value & 1u) ? ~(value >> 1) : (value >> 1));
}

void OFR_EntropyAcm::decode_block(int32_t* dest, uint32_t count, OFR_RangeCoder* rc) {
    for (uint32_t i = 0; i < count; ) {
        for (uint32_t ch = 0; ch < channels; ++ch, ++i)
            dest[i] = (int32_t)decode_sample(chans[ch], rc);
    }
}
