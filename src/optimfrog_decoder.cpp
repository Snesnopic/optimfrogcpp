#include "../include/optimfrog_decoder.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

OFR_DecoderEngine::~OFR_DecoderEngine() {}

struct BitStream {
    ReadInterfaceWrapper* io;
    BitStream(ReadInterfaceWrapper* wrapper) : io(wrapper) {}
    
    bool readBytes(void* dst, uint32_t count) {
        if (!io || !io->rInt) return false;
        return io->rInt->read(io->readerInstance, dst, count) == (int32_t)count;
    }
    
    uint8_t readU8() {
        uint8_t v = 0;
        readBytes(&v, 1);
        return v;
    }
    
    uint16_t readU16() {
        uint16_t v = 0;
        readBytes(&v, 2);
        return v;
    }
    
    uint32_t readU32() {
        uint32_t v = 0;
        readBytes(&v, 4);
        return v;
    }
};

bool OFR_DecoderEngine::open(ReadInterfaceWrapper* wrapper) {
    if (!wrapper || !wrapper->rInt) return false;
    
    BitStream bs(wrapper);
    
    // find 'OFR '
    uint32_t magic = bs.readU32();
    if (magic == 0x4649522a) return false; // '*RIF'
    
    int limit = 0x100000;
    while (limit > 0) {
        if (magic == 0x2052464f) break; // 'OFR '
        magic = (magic >> 8) | ((uint32_t)bs.readU8() << 24);
        limit--;
    }
    if (limit == 0) return false; // not found
    
    uint32_t size = bs.readU32();
    if (size == 0xc) return false; // OFR 4.5alpha
    if (size < 0xf || size > 0x4f) return false; // invalid
    
    uint32_t ts_low = bs.readU32();
    uint16_t ts_high = bs.readU16();
    this->total_samples = ((uint64_t)ts_high << 32) | ts_low;
    
    this->sample_type = bs.readU8();
    this->channelConfig = bs.readU8();
    this->samplerate = bs.readU32();
    
    uint16_t val1 = bs.readU16();
    uint16_t val2 = bs.readU16();
    
    this->version = (val1 >> 4) + 0x1194;
    this->pad2 = val1 & 0xf;
    this->speedup = val2 & 7;
    this->method = val2 >> 3;
    
    int bytesRead = 0xf;
    if (size >= 0x11) {
        uint16_t extra_version = bs.readU16(); // not used according to decomp, just skipped
        bytesRead = 0x11;
    }
    
    // bitspersample mapped from sample_type
    if (this->sample_type == 0 || this->sample_type == 1) this->bitspersample = 8;
    else if (this->sample_type == 2 || this->sample_type == 3) this->bitspersample = 16;
    else if (this->sample_type == 4 || this->sample_type == 5) this->bitspersample = 24;
    else if (this->sample_type == 6 || this->sample_type == 7) this->bitspersample = 32;
    else this->bitspersample = 16; // fallback
    
    // channels mapped from channelConfig
    if (this->channelConfig == 0) this->channels = 1;
    else if (this->channelConfig == 1) this->channels = 2;
    else this->channels = 2; // fallback
    
    // skip remaining header
    if (size > bytesRead) {
        uint32_t to_skip = size - bytesRead;
        auto* rInt = wrapper->rInt;
        auto* rInst = wrapper->readerInstance;
        rInt->seek(rInst, rInt->getPos(rInst) + to_skip);
    }
    
    uint32_t headMagic = bs.readU32();
    if (headMagic == 0x44414548) { // "HEAD"
        this->headSize = bs.readU32();
        if (this->headData) {
            bs.readBytes(this->headData, this->headSize);
        } else {
            auto* rInt = wrapper->rInt;
            auto* rInst = wrapper->readerInstance;
            rInt->seek(rInst, rInt->getPos(rInst) + this->headSize);
        }
        
        this->tailSize = 0;
        this->has_recoverable_errors = false;
        
        return true;
    }
    
    return false;
}

bool OFR_DecoderEngine::read(void* dest, uInt32_t count) { return false; }
bool OFR_DecoderEngine::seek(sInt64_t sample_pos) { return false; }
bool OFR_DecoderEngine::readTail() { return false; }
void OFR_DecoderEngine::close() {}
