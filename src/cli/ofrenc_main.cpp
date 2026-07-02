// ofrenc CLI: encode raw PCM to a lossless .ofr file via the public ofr_encode_mono/stereo API.
#include "../../include/optimfrog_decoder.h"
#include "../../include/optimfrog_encoder.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

// setenv is POSIX-only; MSVC has no equivalent besides _putenv_s.
static inline void ofr_setenv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static void enc_one(const std::vector<int32_t>& s, uint32_t nvals, uint32_t rate, int ch, int bps, std::vector<uint8_t>& out) {
    if (ch == 2) ofr_encode_stereo(s.data(), nvals / 2, rate, bps, out);
    else ofr_encode_mono(s.data(), nvals, rate, bps, out);
}

int main(int argc, char** argv) {
    // --float: encode a raw float32 PCM file (IEEE Float / "OFRX") instead of integer PCM.
    // Consumes one positional slot less (no bps arg, always 32-bit float samples).
    bool want_float = false;
    std::vector<char*> pos;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--float") want_float = true;
        else pos.push_back(argv[i]);
    }
    if (pos.size() < 3) {
        fprintf(stderr, "usage: %s in.raw out.ofr samplerate [channels=1] [bps=16] [--float]\n  (set OFR_BEST=1 to search configs; --float ignores bps, always 32-bit float)\n", argv[0]);
        return 2;
    }
    int ch = (pos.size() >= 4) ? atoi(pos[3]) : 1;
    uint32_t rate = (uint32_t)atoi(pos[2]);

    if (want_float) {
        FILE* f = fopen(pos[0], "rb");
        if (!f) { perror("in"); return 1; }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        std::vector<float> s(sz / 4);
        fread(s.data(), 4, s.size(), f); fclose(f);

        std::vector<uint8_t> out;
        bool ok = (ch == 2) ? ofr_encode_stereo_float(s.data(), (uint32_t)(s.size() / 2), rate, out)
                             : ofr_encode_mono_float(s.data(), (uint32_t)s.size(), rate, out);
        if (!ok) { fprintf(stderr, "float encode failed\n"); return 1; }
        FILE* g = fopen(pos[1], "wb");
        if (!g) { perror("out"); return 1; }
        fwrite(out.data(), 1, out.size(), g); fclose(g);
        fprintf(stderr, "wrote %zu bytes (ch=%d, float32)\n", out.size(), ch);
        return 0;
    }

    int bps = (pos.size() >= 5) ? atoi(pos[4]) : 16;
    int bytes = bps / 8;
    FILE* f = fopen(pos[0], "rb");
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
        auto setenvi = [](const char* k, int v){ char b[16]; snprintf(b,16,"%d",v); ofr_setenv(k,b); };
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
    FILE* g = fopen(pos[1], "wb");
    if (!g) { perror("out"); return 1; }
    fwrite(out.data(), 1, out.size(), g); fclose(g);
    fprintf(stderr, "wrote %zu bytes (ch=%d bps=%d)\n", out.size(), ch, bps);
    return 0;
}
