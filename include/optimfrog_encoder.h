#ifndef OPTIMFROG_ENCODER_H
#define OPTIMFROG_ENCODER_H

#include <cstdint>
#include <vector>
#include "optimfrog_decoder.h"

// public encoder entry points (implemented in src/core/optimfrog_encoder.cpp)
// head/tail: opaque bytes embedded verbatim in the HEAD/TAIL container blocks (--headersize/
// --tailsize equivalents), restored bit-exact around the decoded PCM at decode time.
bool ofr_encode_mono(const int32_t* samples, uint32_t n, uint32_t samplerate, int bps, std::vector<uint8_t>& file,
                      const std::vector<uint8_t>& head = {}, const std::vector<uint8_t>& tail = {});
bool ofr_encode_stereo(const int32_t* samples, uint32_t frames, uint32_t samplerate, int bps, std::vector<uint8_t>& file,
                        const std::vector<uint8_t>& head = {}, const std::vector<uint8_t>& tail = {});

// IEEE Float (.off, "OFRX") encoders (src/core/optimfrog_encoder_float.cpp).
bool ofr_encode_mono_float(const float* samples, uint32_t n, uint32_t samplerate, std::vector<uint8_t>& file,
                            const std::vector<uint8_t>& head = {}, const std::vector<uint8_t>& tail = {});
bool ofr_encode_stereo_float(const float* samples, uint32_t frames, uint32_t samplerate, std::vector<uint8_t>& file,
                              const std::vector<uint8_t>& head = {}, const std::vector<uint8_t>& tail = {});

// range encoder: exact dual of OFR_RangeCoder (decoder). 31-bit window to match the
// decoder's post-init range of 0x80000000 (7-bit init + byte renorm at 0x800001).
// carry-counting byte emission (cache + run-of-0xff).
struct OFR_RangeEncoder {
    std::vector<uint8_t> out;
    uint64_t low = 0;
    uint32_t range = 0x80000000u;
    uint8_t cache = 0;
    uint64_t cache_size = 1;   // canonical: first emitted byte is the dummy b0 (decoder ignores it)

    void shift_low() {
        uint32_t carry = (uint32_t)(low >> 31);            // 0 or 1
        uint32_t win = (uint32_t)(low & 0x7FFFFFFFu);
        if (carry != 0 || win < 0x7F800000u) {
            uint8_t temp = cache;
            do { out.push_back((uint8_t)(temp + carry)); temp = 0xFF; } while (--cache_size);
            cache = (uint8_t)((win >> 23) & 0xFF);
        }
        cache_size++;
        low = (uint64_t)((win << 8) & 0x7FFFFFFFu);
    }

    void renorm() {
        while (range < 0x800001u) { shift_low(); range <<= 8; }
    }

    // dual of decode_uniform(max_val)
    void encode_uniform(uint32_t max_val, uint32_t val) {
        if (max_val == 0) return;
        uint32_t step = range / max_val;
        low += (uint64_t)val * step;
        range = (val < max_val - 1) ? step : (range - val * step);
        renorm();
    }

    // dual of read_uniform_bits_internal(bits)
    void encode_bits_internal(uint32_t bits, uint32_t val) {
        if (bits == 0) return;
        uint32_t step = range >> bits;
        uint32_t max_val = (1u << bits) - 1;
        low += (uint64_t)val * step;
        range = (val < max_val) ? step : (range - val * step);
        renorm();
    }

    // dual of read_uniform_bits(bits): low 12-bit chunks first
    void encode_bits(uint32_t bits, uint32_t value) {
        uint32_t shift = 0;
        while (bits > 12) {
            encode_bits_internal(12, (value >> shift) & 0xFFFu);
            shift += 12;
            bits -= 12;
        }
        if (bits > 0) {
            encode_bits_internal(bits, (value >> shift) & ((1u << bits) - 1));
        }
    }

    // dual of read_uniform_split(bits)
    void encode_split(uint32_t bits, uint32_t value) {
        if (bits <= 12) { encode_bits(bits, value); return; }
        encode_bits(12, value & 0xFFFu);
        encode_split(bits - 12, value >> 12);
    }

    // dual of decode_symbol: replicate the exact tree walk + freq updates for the target sym
    void encode_symbol(OFR_ModelContext& ctx, uint32_t sym) {
        uint32_t step = range / ctx.total_freq;
        int L = 0; while ((1u << L) < ctx.num_nodes) L++;   // num_nodes = 2^L
        uint32_t target = ctx.num_nodes + sym;
        uint32_t node = 1, acc = 0;
        for (int i = L - 1; i >= 0; i--) {
            if (((target >> i) & 1u) == 0) { ctx.freqs[node] += 2; node = 2 * node; }
            else { acc += ctx.freqs[node]; node = 2 * node + 1; }
        }
        uint32_t freq = ctx.freqs[node];
        low += (uint64_t)acc * step;
        range = (acc + freq < ctx.total_freq) ? (step * freq) : (range - acc * step);
        renorm();
        ctx.freqs[node] += 2;
        ctx.total_freq += 2;
        if (ctx.total_freq >= ctx.limit) ctx.halve_freqs();
    }

    // dual of read_golomb (note: this golomb never emits 1 — values are 0, 2, 3, 4, ...)
    void encode_golomb(uint32_t v) {
        if (v == 0) { encode_bits_internal(1, 1); return; }
        int k = 0; while ((1u << (k + 1)) <= v) k++;
        for (int i = 0; i < k; i++) encode_bits_internal(1, 0);
        encode_bits_internal(1, 1);
        encode_bits(k, v & ((1u << k) - 1));
    }

    void flush() {
        for (int i = 0; i < 5; i++) shift_low();
    }
};

#endif
