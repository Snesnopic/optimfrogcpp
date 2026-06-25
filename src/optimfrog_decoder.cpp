#include "../include/optimfrog_decoder.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <algorithm>

// FUN_00018d80: bit-length of the data range (min,max) → entropy bit depth / shift source
static int ofr_bitlen(int mn, int mx) {
    uint32_t a = (uint32_t)((mn >> 31) ^ mn);
    uint32_t b = (uint32_t)((mx >> 31) ^ mx);
    if (a == 0 && b == 0) return 1;
    uint32_t m = ((int)b <= (int)a) ? a : b;
    uint32_t u = (m < 0x10000u) ? m : (m >> 16);
    uint32_t bits = (m > 0xffffu) ? 16u : 0u;
    if (u > 0xffu) { u >>= 8; bits += 8; }
    if (u > 0xfu)  { u >>= 4; bits += 4; }
    if (u > 3u)    { u >>= 2; bits += 2; }
    return (int)(bits + 2u + (u > 1u ? 1u : 0u));
}

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
    for (uint32_t i = 0; i < samples_to_decode; ++i) {
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

            uint32_t entropy_type = uVar15 >> 11;
            uint32_t post_type = uVar15 & 0x3f;
            uint32_t pred_type = (uVar15 >> 6) & 0x1f;
            uint32_t reduced_bit_depth = this->bitspersample;

            // PostProcessor
            if (post_type != 0) {
                if (!this->block_decoder.post_processor) {
                    this->block_decoder.post_processor = new OFR_PostProcessor();
                }
                this->block_decoder.post_processor->init(&this->range_coder, reduced_bit_depth, this->channels);
            }

            // data bit-length (local_4c in FUN_00010870) drives predictor shift and entropy depth
            int data_bits = 13;
            if (post_type != 0) {
                OFR_PostProcessor* pp = this->block_decoder.post_processor;
                if (this->channels == 2) {
                    int mn = std::min(pp->min_val_L, pp->min_val_R);
                    int mx = std::max(pp->max_val_L, pp->max_val_R);
                    data_bits = ofr_bitlen(mn, mx);
                } else {
                    data_bits = ofr_bitlen(pp->min_val_L, pp->max_val_L);
                }
            }

            // Predictor — dispatch on channels
            if (pred_type == 1) {
                if (this->channels == 1) {
                    if (!this->block_decoder.predictor) {
                        this->block_decoder.predictor = new OFR_Predictor();
                    }
                    OFR_Predictor* pd = this->block_decoder.predictor;
                    pd->init(&this->range_coder, this->bitspersample);
                    // FUN_00006f50: min/max/shift from post-processor
                    if (post_type != 0) {
                        OFR_PostProcessor* pp = this->block_decoder.post_processor;
                        pd->min_val = pp->min_val_L;
                        pd->max_val = pp->max_val_L;
                    }
                    pd->shift = 32 - data_bits;
                } else {
                    if (!this->block_decoder.predictor_stereo) {
                        this->block_decoder.predictor_stereo = new OFR_PredictorStereo();
                    }
                    OFR_PredictorStereo* ps = this->block_decoder.predictor_stereo;
                    ps->init(&this->range_coder, this->bitspersample);
                    ps->init_from_bitstream(&this->range_coder);
                    // FUN_00007400: copy postproc min/max and shift into predictor
                    OFR_PostProcessor* pp = this->block_decoder.post_processor;
                    ps->m_left_min  = pp->min_val_L;
                    ps->m_left_max  = pp->max_val_L;
                    ps->m_right_min = pp->min_val_R;
                    ps->m_right_max = pp->max_val_R;
                    ps->m_shift = 32u - (uint32_t)data_bits;
                }
            } else if (pred_type == 3 && this->channels == 1) {
                if (!this->block_decoder.predictor_cascade_mono) {
                    this->block_decoder.predictor_cascade_mono = new OFR_PredictorCascadeMono();
                }
                OFR_PostProcessor* pp = this->block_decoder.post_processor;
                int mn = pp ? pp->min_val_L : -0x800000;
                int mx = pp ? pp->max_val_L : 0x7fffff;
                this->block_decoder.predictor_cascade_mono->init(
                    &this->range_coder, this->bitspersample, mn, mx, data_bits, (int)uncompressed_size);
            } else if (pred_type == 3 && this->channels == 2) {
                if (!this->block_decoder.predictor_cascade_stereo) {
                    this->block_decoder.predictor_cascade_stereo = new OFR_PredictorCascadeStereo();
                }
                OFR_PostProcessor* pp = this->block_decoder.post_processor;
                this->block_decoder.predictor_cascade_stereo->init(
                    &this->range_coder, this->bitspersample,
                    pp->min_val_L, pp->max_val_L, pp->min_val_R, pp->max_val_R,
                    data_bits, (int)uncompressed_size);
            }

            // Entropy Coder
            if (entropy_type != 0) {
                if (!this->block_decoder.entropy) {
                    this->block_decoder.entropy = new OFR_EntropyDecoder();
                    this->block_decoder.entropy->init((uint32_t)data_bits, this->channels, entropy_type, true);
                }
                this->block_decoder.entropy->init_from_bitstream(&this->range_coder);
            }

            this->block_size = uncompressed_size / this->channels;
            this->block_pos = 0;
            this->need_new_block = false;
            this->block_error = false;
        }

        uint32_t remaining_in_block = this->block_size - this->block_pos;
        uint32_t to_read = count - read_so_far;
        if (remaining_in_block < to_read) {
            to_read = remaining_in_block;
        }

        // dest holds int32 interleaved samples; advance by frames*channels
        int32_t* dest_ptr = (int32_t*)dest + (size_t)read_so_far * this->channels;

        if (!this->block_error) {
            this->block_decode(dest_ptr, to_read);

            if (!this->block_error) {
                read_so_far += to_read;
            } else {
                memset(dest_ptr, 0, (size_t)to_read * this->channels * sizeof(int32_t));
            }
        } else {
            memset(dest_ptr, 0, (size_t)to_read * this->channels * sizeof(int32_t));
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
