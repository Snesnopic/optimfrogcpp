#ifndef OPTIMFROG_DECODER_H
#define OPTIMFROG_DECODER_H

#include "OptimFROG.h"
#include <cstdint>

struct ReadInterfaceWrapper {
    ReadInterface* rInt;
    void* readerInstance;
};

class OFR_DecoderEngine {
public:
    virtual ~OFR_DecoderEngine();
    virtual bool open(ReadInterfaceWrapper* wrapper);
    virtual bool read(void* dest, uInt32_t count);
    virtual bool seek(sInt64_t sample_pos);
    virtual bool readTail();
    virtual void close();

    sInt64_t total_samples; // 0x8
    uint8_t stream_stuff[0x48 - 0x10]; // 0x10
    void* headData; // 0x48
    uInt32_t headSize; // 0x50
    uint32_t pad1; // 0x54
    void* tailData; // 0x58
    uInt32_t tailSize; // 0x60
    uint32_t sample_type; // 0x64
    uint32_t channelConfig; // 0x68
    uInt32_t samplerate; // 0x6C
    uInt32_t version; // 0x70
    uint32_t pad2; // 0x74
    uint32_t method; // 0x78
    uint32_t speedup; // 0x7C
    uInt32_t channels; // 0x80
    uInt32_t bitspersample; // 0x84
    bool has_recoverable_errors; // 0x88
    uint8_t pad3[7]; // 0x89
};

struct OptimFROG_InternalState {
    sInt32_t bitrate; // 0x0
    sInt32_t pad0;    // 0x4
    sInt64_t noPoints; // 0x8
    sInt64_t originalSize; // 0x10
    sInt64_t compressedSize; // 0x18
    sInt64_t length_ms; // 0x20
    sInt64_t points_read_so_far; // 0x28
    condition_t has_tags; // 0x30
    uint8_t pad1[7]; // 0x31

    OptimFROG_Tags tags; // 0x38

    sInt64_t unknown_440; // 0x440

    OFR_DecoderEngine* pInterface; // 0x448
    bool is_opened; // 0x450
    bool tail_read; // 0x451
};

#endif
