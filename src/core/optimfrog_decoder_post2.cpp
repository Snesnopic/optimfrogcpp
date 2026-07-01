// post_type=2 — faithful port incl. the value-remap table for tonal/synthetic signals.
// init2 = FUN_00017c40; remap build = FUN_00017f00 + FUN_000189f0; apply in OFR_PostProcessor::decode.
// See doc/post2_remap_analysis.md.

#include "../../include/optimfrog_decoder.h"
#include <cmath>
#include <vector>
#include <cstring>

namespace {
struct PCtx { std::vector<uint32_t> f; uint32_t ns=0, total=0, limit=0, nn=0; };
uint32_t pctx_rebuild(std::vector<uint32_t>& f, uint32_t node, uint32_t nn) {
    if (node < nn) { uint32_t l=pctx_rebuild(f,node*2,nn), r=pctx_rebuild(f,node*2+1,nn); f[node]=l; return l+r; }
    return f[node];
}
void pctx_init(PCtx& c, uint32_t ns, uint32_t lim) {
    c.ns=ns; uint32_t nn=1; while(nn<ns) nn<<=1; c.nn=nn; c.limit=lim;
    c.f.assign(nn*2,0); for(uint32_t i=0;i<ns;i++) c.f[nn+i]=1; c.total=ns; pctx_rebuild(c.f,1,nn);
}
void pctx_halve(PCtx& c) {
    c.total=0; for(uint32_t i=0;i<c.ns;i++){uint32_t&x=c.f[c.nn+i]; x=((x-1)>>1)+1; c.total+=x;} pctx_rebuild(c.f,1,c.nn);
}
uint32_t pctx_decode(PCtx& c, OFR_RangeCoder* rc) {
    rc->normalize();
    uint32_t step=rc->range/c.total, v=rc->value/step, bound=(v<c.total)?v:c.total-1;
    auto& f=c.f; uint32_t node=1, acc=0;
    do { uint32_t nv=f[node], s=nv+acc, ch; if(bound<s){f[node]=nv+2;ch=node;}else{ch=node+1;acc=s;} node=node+ch; } while(node<c.nn);
    uint32_t leaf=f[node], as=step*acc;
    rc->value-=as; if(acc+leaf<c.total) rc->range=step*leaf; else rc->range-=as;
    f[node]+=2; c.total+=2; if(c.limit<=c.total) pctx_halve(c);
    return node-c.nn;
}
uint32_t uni(OFR_RangeCoder* rc, uint32_t N) {
    rc->normalize();
    uint32_t step=rc->range/N, q=rc->value/step, qc=(q<N)?q:N-1;
    rc->value-=qc*step; if(qc+1<N) rc->range=step; else rc->range-=qc*step;
    return qc;
}
uint32_t read32(OFR_RangeCoder* rc) {  // FUN_00016bf0: 4 bytes, little-endian
    uint32_t b0=uni(rc,256), b1=uni(rc,256), b2=uni(rc,256), b3=uni(rc,256);
    return b0 | (b1<<8) | (b2<<16) | (b3<<24);
}
int32_t sext(uint32_t v, uint32_t bits) { uint32_t s=32-bits; return ((int32_t)(v<<s))>>s; }

int xform(int idx, double scale, int v) {
    double sv = (double)v * scale;
    switch (idx) {
        case 1: return (int)std::ceil(sv);
        case 2: return v < 0 ? (int)std::ceil(sv) : (int)std::floor(sv);
        case 3: return v < 0 ? (int)std::floor(sv) : (int)std::ceil(sv);
        case 4: return (int)std::floor(sv + 0.5);
        case 5: return (int)std::lrint(sv);
        default: return (int)std::floor(sv);   // 0, 6
    }
}

// FUN_00017f00: build present[] (indexed v-A) for [A..B]
void build_present(OFR_RangeCoder* rc, int A, int B, int tidx, double scale, std::vector<uint8_t>& present) {
    int n = B - A + 1;
    std::vector<uint8_t> cand(n, 0);
    for (int v = A; v <= B; ++v) { int c = xform(tidx, scale, v); if (c >= A && c <= B) cand[c-A] = 1; }
    present.assign(n, 0);
    PCtx c0,c1,c2,c3; pctx_init(c0,2,0x8000); pctx_init(c1,2,0x8000); pctx_init(c2,2,0x8000); pctx_init(c3,3,0x8000);
    auto gp = [&](int v){ return (v>=A && v<=B) ? present[v-A] : (uint8_t)0; };
    auto gc = [&](int v){ return (v>=A && v<=B) ? cand[v-A] : (uint8_t)0; };
    for (int v = A; v <= B; ++v) {
        int prevP = gp(v-1), nextC = gc(v+1);
        if (cand[v-A] == 1) { present[v-A] = 1; continue; }
        uint32_t b = pctx_decode(c0, rc);
        present[v-A] = (uint8_t)b;
        if (b != 0 && (nextC || prevP)) {
            if (prevP == 0 && nextC == 1) {
                if (pctx_decode(c1, rc) == 0) { present[(v+1)-A] = 0; v++; }
            } else if (prevP == 1 && nextC == 0) {
                if (pctx_decode(c2, rc) == 0) { present[(v-1)-A] = 0; }
            } else {
                uint32_t s = pctx_decode(c3, rc);
                if (s == 1) { present[(v+1)-A] = 0; v++; }
                else if (s == 0) { present[(v-1)-A] = 0; }
            }
        }
    }
}

// FUN_000189f0: build inverse table (dense index -> sparse value) over [A..B]; sets out_min/out_max.
void build_inverse(int A, int B, bool flag, const std::vector<uint8_t>& present,
                   std::vector<int32_t>& table, int& lo, int& out_min, int& out_max) {
    int n = B - A + 1;
    table.assign(n, 0x0fffffff);
    lo = A;
    auto at = [&](int idx) -> int32_t& { return table[idx - A]; };
    if (!flag) {
        for (int v = A; v <= B; ++v) at(v) = v;
        out_min = A; out_max = B;
    } else {
        at(0) = 0;
        int count = 1;
        for (int i = 1; i <= B; ++i) if (i >= A && present[i-A]) { at(count) = i; count++; }
        out_max = count - 1;
        int count2 = -1;
        for (int i = -1; i >= A; --i) if (i <= B && present[i-A]) { at(count2) = i; count2--; }
        out_min = count2 + 1;
    }
}
} // namespace

void OFR_PostProcessor::init2(OFR_RangeCoder* rc, uint32_t channels, uint32_t bit_depth) {
    m_channels = channels;
    has_remap = false;
    int A[2] = {0,0}, B[2] = {0,0};
    bool flag[2] = {false,false};
    std::vector<uint8_t> present[2];

    for (uint32_t ch = 0; ch < channels; ++ch) {
        A[ch] = sext(rc->read_uniform_split(bit_depth), bit_depth);
        B[ch] = sext(rc->read_uniform_split(bit_depth), bit_depth);
        flag[ch] = rc->read_uniform_bits(1) != 0;
        if (flag[ch]) {
            has_remap = true;
            int tidx = (int)uni(rc, 16);
            uint64_t lo32 = read32(rc);
            uint64_t hi32 = read32(rc);
            double scale = (double)(int64_t)((hi32 << 32) | lo32) / 4503599627370496.0; // 2^52
            build_present(rc, A[ch], B[ch], tidx, scale, present[ch]);
        }
    }

    int mn[2], mx[2];
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (has_remap) {
            build_inverse(A[ch], B[ch], flag[ch], present[ch], remap_tbl[ch], remap_lo[ch], mn[ch], mx[ch]);
        } else {
            mn[ch] = A[ch]; mx[ch] = B[ch];
        }
    }

    min_val_L = mn[0]; max_val_L = mx[0]; mult_L = 1; offset_L = 0;
    if (channels == 2) { min_val_R = mn[1]; max_val_R = mx[1]; mult_R = 1; offset_R = 0; }
}
