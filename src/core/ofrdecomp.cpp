#include "../../include/ofrdecomp.hpp"
#include "../../include/OptimFROG.h"
#include "../../include/optimfrog_encoder.h"
#include <algorithm>
#include <cstring>

namespace ofrdecomp {

DecodeError::~DecodeError() = default;

Decoder::Decoder(const std::string& path) {
    instance_ = OptimFROG_createInstance();
    if (!instance_) throw DecodeError("OptimFROG_createInstance failed", OptimFROG_MemoryError);
    condition_t res = OptimFROG_open(instance_, const_cast<char*>(path.c_str()), 0);
    if (res != OptimFROG_NoError) {
        OptimFROG_destroyInstance(instance_);
        instance_ = nullptr;
        throw DecodeError("cannot open " + path, (int)res);
    }
}

Decoder::~Decoder() {
    if (instance_) {
        OptimFROG_close(instance_);
        OptimFROG_destroyInstance(instance_);
    }
}

Decoder::Decoder(Decoder&& other) noexcept : instance_(other.instance_) {
    other.instance_ = nullptr;
}

Decoder& Decoder::operator=(Decoder&& other) noexcept {
    if (this != &other) {
        if (instance_) {
            OptimFROG_close(instance_);
            OptimFROG_destroyInstance(instance_);
        }
        instance_ = other.instance_;
        other.instance_ = nullptr;
    }
    return *this;
}

Info Decoder::info() const {
    OptimFROG_Info raw; std::memset(&raw, 0, sizeof(raw));
    OptimFROG_getInfo(instance_, &raw);
    Info out;
    out.channels = raw.channels;
    out.samplerate = raw.samplerate;
    out.bitspersample = raw.bitspersample;
    out.bitrate = raw.bitrate;
    out.noPoints = raw.noPoints;
    out.originalSize = raw.originalSize;
    out.compressedSize = raw.compressedSize;
    out.length_ms = raw.length_ms;
    return out;
}

size_t Decoder::read(std::span<int32_t> dest, size_t frames) {
    Info i = info();
    size_t channels = i.channels ? i.channels : 1;
    size_t need = frames * channels;
    if (dest.size() < need) throw DecodeError("Decoder::read: destination span too small", -1);

    // OptimFROG_read packs samples into their native byte width (bitspersample/8), not raw
    // int32_t -- unpack + sign-extend here so this API always hands back plain int32 samples.
    int bpv = (int)i.bitspersample / 8;
    std::vector<uint8_t> raw(need * (size_t)bpv);
    int got = OptimFROG_read(instance_, raw.data(), (uInt32_t)frames, 0);
    if (got < 0) throw DecodeError("OptimFROG_read failed", got);

    size_t n = (size_t)got * channels;
    int shift = 32 - (int)i.bitspersample;
    for (size_t k = 0; k < n; k++) {
        int32_t v = 0;
        for (int b = 0; b < bpv; b++) v |= (int32_t)raw[k * (size_t)bpv + b] << (8 * b);
        dest[k] = (v << shift) >> shift;
    }
    return (size_t)got;
}

std::vector<int32_t> Decoder::readAll() {
    Info i = info();
    std::vector<int32_t> out((size_t)i.noPoints * (i.channels ? i.channels : 1));
    const size_t CHUNK = 65536;
    size_t done = 0;
    while (done < (size_t)i.noPoints) {
        size_t want = std::min(CHUNK, (size_t)i.noPoints - done);
        std::span<int32_t> dest(out.data() + done * i.channels, want * i.channels);
        size_t got = read(dest, want);
        if (got == 0) break;
        done += got;
    }
    out.resize(done * (i.channels ? i.channels : 1));
    return out;
}

bool Decoder::seekable() const { return OptimFROG_seekable(instance_) != C_FALSE; }
bool Decoder::seek(int64_t frame) { return OptimFROG_seekPoint(instance_, frame) != C_FALSE; }
bool Decoder::seekTime(int64_t milliseconds) { return OptimFROG_seekTime(instance_, milliseconds) != C_FALSE; }
int64_t Decoder::pos() const { return OptimFROG_getPos(instance_); }
bool Decoder::hadRecoverableErrors() const { return OptimFROG_recoverableErrors(instance_) != C_FALSE; }

namespace Encoder {

std::vector<uint8_t> encodeMono(std::span<const int32_t> samples, uint32_t samplerate, int bitspersample,
                                 std::span<const uint8_t> head, std::span<const uint8_t> tail) {
    std::vector<uint8_t> file;
    std::vector<uint8_t> h(head.begin(), head.end()), t(tail.begin(), tail.end());
    if (!ofr_encode_mono(samples.data(), (uint32_t)samples.size(), samplerate, bitspersample, file, h, t))
        throw DecodeError("ofr_encode_mono failed", -1);
    return file;
}

std::vector<uint8_t> encodeStereo(std::span<const int32_t> interleaved, uint32_t samplerate, int bitspersample,
                                   std::span<const uint8_t> head, std::span<const uint8_t> tail) {
    std::vector<uint8_t> file;
    std::vector<uint8_t> h(head.begin(), head.end()), t(tail.begin(), tail.end());
    uint32_t frames = (uint32_t)(interleaved.size() / 2);
    if (!ofr_encode_stereo(interleaved.data(), frames, samplerate, bitspersample, file, h, t))
        throw DecodeError("ofr_encode_stereo failed", -1);
    return file;
}

} // namespace Encoder
} // namespace ofrdecomp
