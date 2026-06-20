#include "../include/optimfrog_decoder.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

OFR_DecoderEngine::~OFR_DecoderEngine() {}



void OFR_RangeCoder::normalize() {
    OFR_BitStream* bs = (OFR_BitStream*)this->bs_ptr;
    while (range < 0x800001) {
        value = ((cache & 1) << 7) | (value << 8);
        cache = bs->readU8();
        value |= (cache >> 1);
        range <<= 8;
    }
}

void OFR_RangeCoder::init(void* bitstream) {
    this->bs_ptr = bitstream;
    OFR_BitStream* bs = (OFR_BitStream*)bitstream;
    
    // FUN_0011a450 consumes 2 bytes. The first is uVar1 (returned but ignored).
    bs->readU8(); 
    
    // The second is bVar2, which goes into cache.
    this->cache = bs->readU8();
    this->range = 0x80;
    this->value = this->cache >> 1;
    
    // Automatically read bits until range >= 0x800001
    this->normalize();
    
}




bool OFR_DecoderEngine::open(ReadInterfaceWrapper* wrapper) {
    this->bitstream = new OFR_BitStream(wrapper);
    OFR_BitStream* bs = this->bitstream;
    
    uint32_t magic = 0;
    magic = bs->readU32();
    
    int limit = 0x100000;
    while (limit > 0) {
        if (magic == 0x2052464f) break; // 'OFR '
        magic = (magic >> 8) | ((uint32_t)bs->readU8() << 24);
        limit--;
    }
    
    uint32_t size = bs->readU32();
    
    uint32_t ts_low = bs->readU32();
    uint16_t ts_high = bs->readU16();
    this->total_samples = ((uint64_t)ts_high << 32) | ts_low;
    
    this->sample_type = bs->readU8();
    this->channelConfig = bs->readU8();
    this->samplerate = bs->readU32();
    
    uint16_t val1 = bs->readU16();
    uint8_t val2 = bs->readU8();
    
    this->version = (val1 >> 4) + 0x1194;
    this->pad2 = val1 & 0xf;
    this->speedup = val2 & 7;
    this->method = val2 >> 3;
    
    int bytesRead = 0xf;
    if (size >= 0x11) {
        uint16_t extra_version = bs->readU16(); // not used according to decomp, just skipped
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
        for(uint32_t i=0; i<to_skip; i++) bs->readU8();
    }
    
    uint32_t headMagic = bs->readU32();
    if (headMagic == 0x44414548) { // "HEAD"
        this->headSize = bs->readU32();
        if (this->headData) {
            bs->readBytes(this->headData, this->headSize);
        } else {
            for(uint32_t i=0; i<this->headSize; i++) bs->readU8();
        }
        
        this->tailSize = 0;
        this->has_recoverable_errors = false;
        this->decode_buffer = new int32_t[1024 * 1024];
        this->samples_read_so_far = 0;
        this->need_new_block = true;
        
        return true;
    }
    
    return false;
}


uInt32_t OFR_DecoderEngine::block_decode(void* dest, uInt32_t count) {
    uint32_t samples_to_decode = count * this->channels;
    this->block_decoder.decode_block((uint32_t*)this->decode_buffer, samples_to_decode, &this->range_coder);
    
    int32_t* dest32 = (int32_t*)dest;
    int32_t* src32 = this->decode_buffer;
    uint32_t num_samples = samples_to_decode * this->channels;
    for (uint32_t i = 0; i < num_samples; ++i) {
        dest32[i] = src32[i];
    }

    
    return count;
}

uInt32_t OFR_DecoderEngine::read(void* dest, uInt32_t count) {
    if (count == 0 || this->samples_read_so_far >= this->total_samples) {
        return 0;
    }

    OFR_BitStream* bs = this->bitstream;

    uint32_t read_so_far = 0;
    do {
        if (this->need_new_block) {
            uint32_t comp_magic = bs->readU32();
            if (comp_magic != 0x504d4f43) { // 'COMP'
                this->has_recoverable_errors = true;
                return read_so_far; 
            }

            uint32_t compressed_size = bs->readU32();
            uint32_t skipped = bs->readU32();
            uint32_t uncompressed_size = bs->readU32();
            
            uint8_t uVar8 = bs->readU8();
            uint8_t bVar2 = bs->readU8();
            uint16_t uVar15 = bs->readU16();
            
            
            // Skip 1 byte (possibly CRC-8)
            bs->readU8();
            
            this->range_coder.init(bs);
            
            uint8_t entropy_type = uVar15 & 0x3f;
            uint8_t post_type = uVar15 >> 11;
            uint8_t pred_type = (uVar15 >> 6) & 0x1f;
            
            // PostProcessor
            if (post_type != 0) {
                if (!this->block_decoder.post_processor) {
                    this->block_decoder.post_processor = new OFR_PostProcessor();
                }
                this->block_decoder.post_processor->init(&this->range_coder, this->bitspersample);
            }
            
            // Predictor
            if (pred_type == 1) {
                if (!this->block_decoder.predictor_stereo) {
                    this->block_decoder.predictor_stereo = new OFR_PredictorStereo();
                }
                this->block_decoder.predictor_stereo->init(&this->range_coder, this->bitspersample);
            }
            
            // Entropy Coder
            if (entropy_type != 0) {
                if (!this->block_decoder.entropy) {
                    this->block_decoder.entropy = new OFR_EntropyDecoder();
                    this->block_decoder.entropy->init(this->bitspersample, this->channels, true);
                }
                this->block_decoder.entropy->init_from_bitstream(&this->range_coder);
            }

            this->block_size = uncompressed_size;
            this->block_pos = 0;
            this->need_new_block = false;
            this->block_error = false;
        }

        uint32_t remaining_in_block = this->block_size - this->block_pos;
        uint32_t to_read = count - read_so_far;
        if (remaining_in_block < to_read) {
            to_read = remaining_in_block;
        }

        uint32_t bytes_per_sample = (this->bitspersample / 8) * this->channels;
        uint8_t* dest_ptr = (uint8_t*)dest + read_so_far * bytes_per_sample;

        if (!this->block_error) {
            this->block_decode(dest_ptr, to_read);
            
            if (!this->block_error) {
                read_so_far += to_read;
            } else {
                memset(dest_ptr, 0, to_read * bytes_per_sample);
            }
        } else {
            memset(dest_ptr, 0, to_read * bytes_per_sample);
        }

        this->block_pos += to_read;
        this->samples_read_so_far += to_read;

        if (this->block_pos == this->block_size) {
            this->need_new_block = true;
        }

    } while (read_so_far < count && this->samples_read_so_far < this->total_samples);

    return read_so_far;
}
bool OFR_DecoderEngine::seek(sInt64_t sample_pos) { return false; }
bool OFR_DecoderEngine::readTail() { return false; }
void OFR_DecoderEngine::close() {}
