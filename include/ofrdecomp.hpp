// Modern C++ facade over the flat C SDK (OptimFROG.h) and the encoder entry points
// (optimfrog_encoder.h). Purely additive: a thin wrapper, namespaced, RAII, exceptions,
// std::span/vector instead of manual create/destroy + raw pointers. Does not touch the
// bit-exact core -- see OptimFROG.h for the SDK-compatible flat API this project also exposes.

/// \file ofrdecomp.hpp
/// \brief Namespaced, RAII, exception-based C++ facade for the OptimFROG decoder/encoder.

#ifndef OFRDECOMP_HPP
#define OFRDECOMP_HPP

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

/// \brief Modern C++ API for the OptimFROG codec (RAII decoder, stateless encoder helpers).
namespace ofrdecomp {

/// \brief Thrown by Decoder on open/read failures.
///
/// what() carries a short human-readable description; code() is one of the OptimFROG_*
/// result codes (see OptimFROG.h) when known, otherwise -1.
class DecodeError : public std::runtime_error {
public:
    /// \param what human-readable description of the failure.
    /// \param code an OptimFROG_* result code (OptimFROG.h), or -1 if not applicable.
    DecodeError(const std::string& what, int code) : std::runtime_error(what), code_(code) {}
    ~DecodeError() override; // out-of-line: anchors the vtable to ofrdecomp.cpp

    /// \return the OptimFROG_* result code associated with this failure, or -1.
    int code() const noexcept { return code_; }

private:
    int code_;
};

/// \brief Stream properties reported by Decoder::info().
struct Info {
    uint32_t channels = 0;      ///< Channel count (1 = mono, 2 = stereo).
    uint32_t samplerate = 0;    ///< Sample rate in Hz.
    uint32_t bitspersample = 0; ///< Bit depth of the original PCM (e.g. 16, 24).
    uint32_t bitrate = 0;       ///< Average bitrate in kbps, as reported by the codec.
    int64_t noPoints = 0;       ///< Total decodable length, in frames.
    int64_t originalSize = 0;   ///< Original (uncompressed) size, in bytes.
    int64_t compressedSize = 0; ///< Compressed file size, in bytes.
    int64_t length_ms = 0;      ///< Total length in milliseconds.
};

/// \brief RAII wrapper around OptimFROG_createInstance/open/read/close/destroyInstance.
///
/// Move-only: owns a single opaque decoder instance for the lifetime of the object and
/// releases it in the destructor. Copying is disabled since the underlying instance is not
/// shareable.
class Decoder {
public:
    /// \brief Opens `path` for decoding.
    /// \param path filesystem path to an .ofr/.off/.ofs file.
    /// \throws DecodeError if the instance cannot be created or the file cannot be opened.
    explicit Decoder(const std::string& path);
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&& other) noexcept;
    Decoder& operator=(Decoder&& other) noexcept;

    /// \return the stream's channel/rate/bit-depth/length metadata.
    Info info() const;

    /// \brief Decodes up to `frames` interleaved sample frames into `dest`.
    /// \param dest destination buffer, sized for at least `frames * info().channels` int32_t
    ///             values.
    /// \param frames maximum number of frames to decode.
    /// \return the number of frames actually decoded (0 at end of stream).
    size_t read(std::span<int32_t> dest, size_t frames);

    /// \brief Convenience helper that decodes the entire remaining stream in one call.
    /// \return one interleaved buffer holding every remaining sample frame.
    std::vector<int32_t> readAll();

    /// \return true if the stream supports seeking (OptimFROG_seekable).
    bool seekable() const;

    /// \brief Seeks to an absolute frame index.
    /// \param frame target frame index.
    /// \return true on success.
    bool seek(int64_t frame);

    /// \brief Seeks to an absolute time offset.
    /// \param milliseconds target offset from the start of the stream.
    /// \return true on success.
    bool seekTime(int64_t milliseconds);

    /// \return the current frame position.
    int64_t pos() const;

    /// \return true if the decoder had to recover from a corrupt/unsupported block while
    ///         decoding (output for that block is zero-filled rather than a hard failure).
    bool hadRecoverableErrors() const;

private:
    void* instance_ = nullptr;
};

/// \brief Stateless encode entry points, mirroring ofr_encode_mono/stereo (optimfrog_encoder.h).
///
/// `head`/`tail` are opaque bytes embedded verbatim in the HEAD/TAIL container blocks and
/// restored bit-exact around the decoded PCM (matches the CLI's --headersize/--tailsize).
namespace Encoder {
    /// \brief Encodes mono PCM to a lossless .ofr byte stream.
    /// \param samples signed PCM samples, one per frame.
    /// \param samplerate sample rate in Hz.
    /// \param bitspersample bit depth of `samples` (8, 16, 24, or 32).
    /// \param head bytes to embed verbatim in the HEAD container block.
    /// \param tail bytes to embed verbatim in the TAIL container block.
    /// \return the encoded .ofr file contents.
    std::vector<uint8_t> encodeMono(std::span<const int32_t> samples, uint32_t samplerate, int bitspersample,
                                     std::span<const uint8_t> head = {}, std::span<const uint8_t> tail = {});

    /// \brief Encodes interleaved stereo PCM to a lossless .ofr byte stream.
    /// \param interleaved signed PCM samples, interleaved left/right per frame.
    /// \param samplerate sample rate in Hz.
    /// \param bitspersample bit depth of `interleaved` (8, 16, 24, or 32).
    /// \param head bytes to embed verbatim in the HEAD container block.
    /// \param tail bytes to embed verbatim in the TAIL container block.
    /// \return the encoded .ofr file contents.
    std::vector<uint8_t> encodeStereo(std::span<const int32_t> interleaved, uint32_t samplerate, int bitspersample,
                                       std::span<const uint8_t> head = {}, std::span<const uint8_t> tail = {});
} // namespace Encoder

} // namespace ofrdecomp

#endif
