#include "optimfrog_decoder.h"
#include <cstring>

void OFR_ModelContext::init(uint32_t symbols, uint32_t lim) {
    num_symbols = symbols;
    limit = lim;
    
    num_nodes = 1;
    while (num_nodes < num_symbols) num_nodes <<= 1;
    
    freqs.assign(num_nodes * 2, 0);
    for (uint32_t i = 0; i < num_symbols; ++i) {
        freqs[num_nodes + i] = 1;
    }
    total_freq = num_symbols;
    rebuild_tree(1);
}

void OFR_ModelContext::halve_freqs() {
    total_freq = 0;
    for (uint32_t i = 0; i < num_symbols; ++i) {
        uint32_t& f = freqs[num_nodes + i];
        f = ((f - 1) / 2) + 1;
        total_freq += f;
    }
    rebuild_tree(1);
}

uint32_t OFR_ModelContext::rebuild_tree(uint32_t node) {
    if (num_nodes <= node) {
        return freqs[node];
    }
    uint32_t left = rebuild_tree(node * 2);
    uint32_t right = rebuild_tree(node * 2 + 1);
    freqs[node] = left;
    return left + right;
}


void OFR_EntropyDecoder::init(uint32_t depth, uint32_t ch) {
    bit_depth = depth;
    channels = ch;
    // We do NOT initialize contexts here, we do it in lazy init
}

void OFR_EntropyDecoder::init_from_bitstream(OFR_RangeCoder* rc) {
    uint32_t val = rc->read_uniform_bits(12);
    if (val == 0xfff) {
        val = rc->read_uniform_bits_16() + 0x1001;
    } else {
        val = val + 2;
    }
    
    this->weight = ((double)val - 1.0) / (double)val;
    this->weight2 = 1.0 - this->weight;
    this->needs_init = true;
    this->decoded_samples = 0;
}

static uint32_t read_uniform_bits(OFR_RangeCoder* rc, uint32_t bits) {
    return rc->read_uniform_bits(bits);
}

uint32_t OFR_EntropyDecoder::decode(uint32_t* dest, uint32_t count, OFR_RangeCoder* rc) {
    if (needs_init) {
        uint32_t num_contexts = 32;
        uint32_t num_symbols = bit_depth * 8 - 16;
        if (bit_depth < 4) {
            num_symbols = 1 << bit_depth;
        }
        contexts.resize(num_contexts);
        uint32_t var_factor = bit_depth * 8 - 16;
        if (bit_depth < 4) var_factor = 1 << (bit_depth & 0x1f);
        
        for (auto& ctx : contexts) {
            ctx.init(num_symbols, 32768);
        }
        variance = 0.0;
        variance2 = 0.0;
        needs_init = false;
    }
    uint32_t target_frames = target_samples / channels;
    uint32_t decoded_frames = decoded_samples / channels;
    uint32_t to_decode_frames = target_frames - decoded_frames;
    
    if (count <= to_decode_frames) {
        to_decode_frames = count;
    }
    uint32_t samples_to_loop = to_decode_frames * channels;
    decoded_samples += samples_to_loop;
    
    for (uint32_t i = 0; i < samples_to_loop; ++i) { 
        uint32_t current_variance;
        if (channels == 2 && (i % 2) != 0) {
            current_variance = static_cast<uint32_t>(variance2);
        } else {
            current_variance = static_cast<uint32_t>(variance);
        }
        
        int32_t e_var = 0;
        if (current_variance > 65535) {
            e_var = 16;
            current_variance >>= 16;
        }
        if (current_variance > 255) {
            e_var += 8;
            current_variance >>= 8;
        }
        if (current_variance > 15) {
            e_var += 4;
            current_variance >>= 4;
        }
        if (current_variance > 3) {
            e_var += 2;
            current_variance >>= 2;
        }
        
        uint32_t idx = e_var + (current_variance > 1 ? 1 : 0);
        if (idx > 30) idx = 30;
        
        OFR_ModelContext& ctx = contexts[idx];

        uint32_t symbol = rc->decode_symbol(ctx);

        uint32_t decoded_val = symbol;
        if (symbol > 7) {
            uint32_t extra = 0;
            uint32_t bits = (symbol - 8) >> 3;
            uint32_t chunks = bits;
            uint32_t shift = 0;
            
            while (chunks > 12) {
                uint32_t val = rc->read_uniform_bits(12);
                extra |= val << shift;
                shift += 12;
                chunks -= 12;
            }
            if (chunks > 0) {
                uint32_t val = rc->read_uniform_bits(chunks);
                extra |= val << shift;
            }
            
            uint32_t rem = (symbol - 8) & 7;
            decoded_val = (rem << bits) | (1 << (bits + 3)) | extra;
        }

        int32_t signed_val = decoded_val >> 1;
        if (decoded_val & 1) signed_val = ~signed_val;
        dest[i] = signed_val;
        
        double old_var = (channels == 2 && (i % 2) != 0) ? variance2 : variance;
        double new_var = old_var * weight + (double)decoded_val * (double)decoded_val * weight2 + weight2;
        if (channels == 2 && (i % 2) != 0) {
            variance2 = new_var;
        } else {
            variance = new_var;
        }
    }
    
    
    if (decoded_samples == target_samples) {
        contexts.clear();
        needs_init = true;
        decoded_samples = 0;
    }
    return to_decode_frames;
}
