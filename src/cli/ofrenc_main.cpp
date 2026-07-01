// ofrenc CLI: encode raw PCM to a lossless .ofr file via the public ofr_encode_mono/stereo API.
#include "../../include/optimfrog_decoder.h"
#include "../../include/optimfrog_encoder.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

static void enc_one(const std::vector<int32_t>& s, uint32_t nvals, uint32_t rate, int ch, int bps, std::vector<uint8_t>& out) {
    if (ch == 2) ofr_encode_stereo(s.data(), nvals / 2, rate, bps, out);
    else ofr_encode_mono(s.data(), nvals, rate, bps, out);
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s in.raw out.ofr samplerate [channels=1] [bps=16]\n  (set OFR_BEST=1 to search configs)\n", argv[0]); return 2; }
    int ch = (argc >= 5) ? atoi(argv[4]) : 1;
    int bps = (argc >= 6) ? atoi(argv[5]) : 16;
    uint32_t rate = (uint32_t)atoi(argv[3]);
    int bytes = bps / 8;
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("in"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> raw(sz);
    fread(raw.data(), 1, sz, f); fclose(f);
    uint32_t nvals = (uint32_t)(sz / bytes);
    std::vector<int32_t> s(nvals);
    for (uint32_t i = 0; i < nvals; i++) {
        const uint8_t* p = &raw[(size_t)i * bytes];
        int32_t v = 0;
        for (int b = 0; b < bytes; b++) v |= (int32_t)p[b] << (8 * b);
        int shleft = 32 - bps; v = (v << shleft) >> shleft;
        s[i] = v;
    }

    std::vector<uint8_t> out;
    if (getenv("OFR_BEST")) {
        // search a small set of safe configs, keep the smallest (all paths are verified lossless)
        std::vector<uint8_t> best;
        std::string best_desc;
        auto setenvi = [](const char* k, int v){ char b[16]; snprintf(b,16,"%d",v); setenv(k,b,1); };
        // pred=1 orders (mono up to 96 safe; stereo via od_idx 0/1) x ent 1/2/3, plus pred=3 x ent
        int mono_orders[] = {24, 64, 96};
        int st_odidx[]    = {0, 1};
        for (int ent = 1; ent <= 3; ++ent) {
            setenvi("OFR_ENT", ent);
            // pred=1
            setenvi("OFR_PRED", 1);
            if (ch == 2) {
                for (int oi : st_odidx) { setenvi("OFR_ODIDX", oi); std::vector<uint8_t> o; enc_one(s,nvals,rate,ch,bps,o);
                    if (best.empty() || o.size() < best.size()) { best = o; best_desc = "pred1 ent"+std::to_string(ent)+" odidx"+std::to_string(oi); } }
            } else {
                for (int od : mono_orders) { setenvi("OFR_ORDER", od); std::vector<uint8_t> o; enc_one(s,nvals,rate,ch,bps,o);
                    if (best.empty() || o.size() < best.size()) { best = o; best_desc = "pred1 ent"+std::to_string(ent)+" order"+std::to_string(od); } }
            }
            // pred=3 (fixed cascade params)
            setenvi("OFR_PRED", 3);
            { std::vector<uint8_t> o; enc_one(s,nvals,rate,ch,bps,o);
              if (best.empty() || o.size() < best.size()) { best = o; best_desc = "pred3 ent"+std::to_string(ent); } }
        }
        out = best;
        fprintf(stderr, "best: %s\n", best_desc.c_str());
    } else {
        enc_one(s, nvals, rate, ch, bps, out);
    }
    FILE* g = fopen(argv[2], "wb"); fwrite(out.data(), 1, out.size(), g); fclose(g);
    fprintf(stderr, "wrote %zu bytes (ch=%d bps=%d)\n", out.size(), ch, bps);
    return 0;
}
