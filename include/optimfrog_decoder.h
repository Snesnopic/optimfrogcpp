#ifndef OPTIMFROG_DECODER_H
#define OPTIMFROG_DECODER_H

#include "OptimFROG.h"
#include <cstdint>
#include <vector>
#include <cstring>

struct ReadInterfaceWrapper {
    ReadInterface* rInt;
    void* readerInstance;
};


class OFR_BitStream {
public:
    ReadInterfaceWrapper* wrapper;
    uint8_t buffer[4096];
    uint32_t buf_pos;
    uint32_t buf_avail;
    uint32_t total_read;
    // per-block byte budget for the range coder: -1 = unlimited (header reads); >=0 = payload
    // bytes left (range coder must not over-read into the next COMP block). 0 => EOF-fill with 0.
    long range_budget = -1;

    OFR_BitStream(ReadInterfaceWrapper* w) : wrapper(w), buf_pos(0), buf_avail(0), total_read(0) {}

    uint8_t readByte() {
        if (range_budget == 0) return 0;
        if (range_budget > 0) range_budget--;
        if (buf_avail == 0) {
            buf_avail = wrapper->rInt->read(wrapper->readerInstance, buffer, sizeof(buffer));
            buf_pos = 0;
            if (buf_avail == 0) return 0;
        }
        buf_avail--;
        total_read++;
        return buffer[buf_pos++];
    }

    bool readBytes(void* dst, uint32_t count) {
        uint8_t* d = (uint8_t*)dst;
        for (uint32_t i = 0; i < count; i++) {
            d[i] = readByte();
        }
        return true;
    }

    uint8_t readU8() {
        return readByte();
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

    uint32_t tell() const {
        return total_read;
    }
};


struct OFR_ModelContext {
    std::vector<uint32_t> freqs;
    uint32_t num_symbols;
    uint32_t total_freq;
    uint32_t limit;
    uint32_t num_nodes;

    void init(uint32_t symbols, uint32_t lim);
    void halve_freqs();
    uint32_t rebuild_tree(uint32_t node);
};

struct OFR_RangeCoder {
    uint32_t value;
    uint32_t range;
    uint8_t  cache;
    void* bs_ptr;

    void init(void* bitstream);
    
    void normalize();
    uint32_t read_uniform_split(uint32_t bits) {
        if (bits <= 12) return read_uniform_bits(bits);
        uint32_t val1 = read_uniform_bits(12);
        uint32_t val2 = read_uniform_split(bits - 12);
        return (val2 << 12) | val1;
    }

    uint32_t read_uniform_bits(uint32_t bits) {
        uint32_t ret = 0;
        uint32_t shift = 0;
        while (bits > 12) {
            ret |= (read_uniform_bits_internal(12) << shift);
            shift += 12;
            bits -= 12;
        }
        if (bits > 0) {
            ret |= (read_uniform_bits_internal(bits) << shift);
        }
        return ret;
    }
    
    uint32_t read_golomb() {
        uint32_t k = 0;
        while (read_uniform_bits(1) == 0) {
            k++;
        }
        if (k == 0) return 0;
        return (1u << k) | read_uniform_bits(k);
    }
    
    uint32_t read_uniform_bits_16() {
        uint32_t val1 = read_uniform_bits(8);
        uint32_t val2 = read_uniform_bits(8);
        return val1 | (val2 << 8);
    }
    

    uint32_t read_uniform_bits_internal(uint32_t bits) {
        if (bits == 0) return 0;
        normalize();
        uint32_t step = this->range >> bits;
        uint32_t val = this->value / step;
        uint32_t max_val = (1u << bits) - 1;

        if (val < max_val) {
            this->value -= val * step;
            this->range = step;
        } else {
            val = max_val;
            this->value -= val * step;
            this->range -= val * step;
        }
        return val;
    }
    uint32_t decode_symbol(OFR_ModelContext& ctx) {
        normalize();
        uint32_t step = this->range / ctx.total_freq;
        uint32_t val = this->value / step;
        if (val >= ctx.total_freq) {
            val = ctx.total_freq - 1;
        }
        
        uint32_t node = 1;
        uint32_t accumulated = 0;
        
        while (node < ctx.num_nodes) {
            uint32_t left_freq = ctx.freqs[node];
            if (val >= accumulated + left_freq) {
                accumulated += left_freq;
                node = node * 2 + 1;
            } else {
                ctx.freqs[node] += 2;
                node = node * 2;
            }
        }
        
        uint32_t sym = node - ctx.num_nodes;
        uint32_t freq = ctx.freqs[node];
        
        this->value -= accumulated * step;
        if (accumulated + freq < ctx.total_freq) {
            this->range = step * freq;
        } else {
            this->range -= accumulated * step;
        }
        
        ctx.freqs[node] += 2;
        ctx.total_freq += 2;
        
        if (ctx.total_freq >= ctx.limit) {
            ctx.halve_freqs();
        }
        
        return sym;
    }

    uint32_t read_12bit_value() {
        uint32_t uVar11 = decode_uniform(4096); // 12 bits means range 0..4095
        if (uVar11 == 0xfff) {
            uVar11 = decode_uniform(65536) + 0x1001; // 16 bits means range 0..65535
        } else {
            uVar11 += 2;
        }
        return uVar11;
    }

    uint32_t decode_uniform(uint32_t max_val) {
        if (max_val == 0) return 0;
        normalize();
        uint32_t step = this->range / max_val;
        uint32_t val = this->value / step;
        
        if (val < max_val - 1) {
            this->value -= val * step;
            this->range = step;
        } else {
            val = max_val - 1;
            this->value -= val * step;
            this->range -= val * step;
        }
        return val;
    }
    uint32_t get_pos() { return ((OFR_BitStream*)bs_ptr)->tell(); }
};




class OFR_EntropyDecoder {
public:
    bool is_fast_stereo;
    double weight;
    double weight2;
    double variance;
    double variance2;
    uint32_t bit_depth;
    uint32_t channels;
    uint32_t decoded_samples;
    uint32_t type;
    bool needs_init;

    // 9 sub-contexts (FUN_00113f50(lVar16+0x58, 9, 9, 0x8000))
    std::vector<OFR_ModelContext> contexts;

    // Master adaptive Huffman context (at param_1+0x30 in FUN_00109ab0)
    std::vector<uint32_t> master_freqs;
    uint32_t master_num_symbols;  // *(param_1+0x3c)
    uint32_t master_total_freq;   // running sum, updated on decode
    uint32_t master_limit;        // *(param_1+0x40)
    uint32_t master_num_nodes;    // *(param_1+0x44)

    // Escape adaptive Huffman context
    std::vector<uint32_t> escape_freqs;
    uint32_t escape_num_symbols;
    uint32_t escape_total_freq;
    uint32_t escape_limit;
    uint32_t escape_num_nodes;

    OFR_EntropyDecoder() : is_fast_stereo(false), weight(0.0), weight2(0.0),
        variance(1.0), variance2(1.0), bit_depth(0), channels(1),
        decoded_samples(0), needs_init(true),
        master_num_symbols(32), master_total_freq(32),
        master_limit(0x8000), master_num_nodes(32),
        escape_num_symbols(32), escape_total_freq(32),
        escape_limit(0x8000), escape_num_nodes(32) {}

    void init(uint32_t depth, uint32_t ch, uint32_t entropy_type, bool fast_stereo);
    void init_from_bitstream(OFR_RangeCoder* rc);
    int32_t decode_block(int32_t* dest, uint32_t count, OFR_RangeCoder* rc);

    // Core per-sample decode (mirrors FUN_00109ab0)
    int32_t decode_one_sample(double& var, OFR_RangeCoder* rc);

    // --- fast stereo path (FUN_00004710 / FUN_00005ef0) ---
    struct FastCtx {
        std::vector<uint32_t> freqs;
        uint32_t num_symbols;
        uint32_t total_freq;
        uint32_t limit;
        uint32_t num_nodes;
    };
    std::vector<FastCtx> fast_contexts;
    double fast_var_L;
    double fast_var_R;
    bool fast_inited;
    int32_t fast_decode_sample(double& var, OFR_RangeCoder* rc);

private:
    uint32_t rebuild_master(uint32_t node);
    void     halve_master();
};


class OFR_Predictor {
public:

    int order;
    int update_interval;
    double damping;
    uint32_t sample_count;
    uint32_t last_update_count;
    uint32_t fail_count;
    int shift;
    int min_val;
    int max_val;
    int integrate;
    int last_int[8];

    void init(OFR_RangeCoder* rc, uint32_t bit_depth);
    
    std::vector<double> history_buffer;
    double* history_head;
    
    std::vector<double> vector_R;
    std::vector<double> weights;
    std::vector<double> temp_D;
    std::vector<std::vector<double>> inst_matrix;
    std::vector<std::vector<double>> cov_matrix;


    OFR_Predictor();

    void init(int ord, int interval, double damp);
    double predict();
    void update(double sample);
    void update_weights();
    void update_cov_1();
    bool solve_ldlt();
    
    void decode(int* dest, int count);
};


typedef void (*ProgressCallback)(double);

struct OFR_PredictorStereo_Inner {
    uint32_t m_count_left;           // 0x00
    uint32_t m_count_right;          // 0x04
    uint32_t m_update_count_left;    // 0x08
    uint32_t m_update_count_right;   // 0x0C
    int32_t m_max_order;             // 0x10
    int32_t m_left_order;            // 0x14
    int32_t m_right_order;           // 0x18
    uint32_t m_update_interval;      // 0x1C
    uint32_t m_unknown20;            // 0x20
    uint32_t m_unknown24;            // 0x24
    double* m_left_hist_ptr;         // 0x28
    double* m_right_hist_ptr;        // 0x30
    double m_weight;                 // 0x38

    double m_R_left[258];            // 0x40
    double m_R_right[258];           // 0x850

    double m_left_coefs[128];        // 0x1060
    double m_right_coefs[128];       // 0x1460
    double m_temp_vector[128];       // 0x1860

    double m_left_matrix[128][128];  // 0x1C60
    double m_right_matrix[128][128]; // 0x21C60
    double m_temp_matrix[128][128];  // 0x41C60

    double m_left_history[1024];     // 0x61C60
    double m_right_history[1024];    // 0x63C60

    uint8_t m_is_cholesky_fail_left; // 0x65C60
    uint8_t m_is_cholesky_fail_right;// 0x65C61
    uint8_t pad[13];                 // 0x65C62

    void init(double weight, int max_order, int left_order, uint32_t update_interval);
    double predictLeft();
    double predictRight();
    void updateLeft(double sample);
    void updateRight(double sample);

    void updateAutocorrLeft();
    void updateAutocorrRight();
    void solveCholeskyLeft();
    void solveCholeskyRight();
};

struct OFR_PredictorStereo {
    void* vtable;                    // 0x00
    uint8_t pad_08[8];               // 0x08
    OFR_PredictorStereo_Inner m_inner; // 0x10
    uint32_t m_weight_param;         // 0x65C80
    int32_t m_max_order;             // 0x65C84
    int32_t m_right_order;           // 0x65C88
    uint32_t m_update_interval;      // 0x65C8C
    int32_t m_left_min;              // 0x65C90
    int32_t m_left_max;              // 0x65C94
    int32_t m_right_min;             // 0x65C98
    int32_t m_right_max;             // 0x65C9C
    uint32_t m_shift;                // 0x65CA0
    int32_t m_sample_counter;        // 0x65CA4
    bool m_need_init;                // 0x65CA8
    uint8_t pad_ca9[7];              // 0x65CA9
    ProgressCallback m_progress_cb;  // 0x65CB0
    bool m_is_fast;                  // 0x65CB8
    void init(OFR_RangeCoder* rc, uint32_t bit_depth);
    void init_from_bitstream(OFR_RangeCoder* rc) {
        m_need_init = true;
    }
    void decode(int32_t* outputs, uint32_t num_samples);
};

class OFR_PredictorFastStereo {
public:
    void init(OFR_RangeCoder* rc, uint32_t bit_depth);
    OFR_Predictor left_predictor;
    OFR_Predictor right_predictor;
    int cross_channel; // param_1 + 0x6718c
    int flag; // param_1 + 0x67188
    
    OFR_PredictorFastStereo() : cross_channel(0), flag(0) {}
    
    void decode(int* dest, int count);
};


// pred_type=3: dual predictor (LPC + cascade NLMS). See doc/pred3_analysis.md.
struct OFR_CascadeStage {
    std::vector<float> weights;   // order taps
    int order;
    double energy;                // sliding energy (binary stores double via MOVSD)
    double mu;
    double eps;
    std::vector<float> ring;      // 0x400 floats
    int ring_cur;                 // index of current head
    int ring_copy;                // order+1
};

class OFR_PredictorCascadeMono {
public:
    OFR_Predictor main_lpc;       // the +0x10 LPC

    int n_stages = 0;
    std::vector<OFR_CascadeStage> stages;   // 1-based (index 0 unused)
    double bias = 0.0, decay = 1.0;
    std::vector<double> stage_pred;         // [s]
    std::vector<double> errv;               // [s]
    std::vector<double> cumsum;             // [s]

    // final combiner (NLMS over cumulative stage outputs)
    uint32_t fc_counter = 0;
    uint32_t fc_last_halve = 0;
    int fc_size = 0;
    uint32_t fc_halve_interval = 0;
    double fc_decay = 1.0;
    std::vector<double> fc_coefs;           // size fc_size (the +0x268 array)
    std::vector<std::vector<double>> fc_mat; // covariance M (lower-tri), exp-weighted
    std::vector<double> fc_v;                // cross-correlation V[0..fc_size]
    void fc_solve();

    int min_val = 0, max_val = 0, shift = 0;
    int total_samples = 0;
    uint32_t main_weight_param = 0, main_order = 0, main_interval = 0;

    int seg_len = 0;       // 0x436ac
    int cc_count = 0;      // 0x436b4 countdown
    int cc_mode = 0;       // 0x436b0
    uint32_t sched_idx = 0; // 0x436b8
    std::vector<uint8_t> schedule;  // 0x437b8
    bool need_init = false;
    uint32_t sample_counter = 0;

    void init(OFR_RangeCoder* rc, uint32_t bit_depth, int mn, int mx, int dbits, int total);
    void decode(int32_t* dest, uint32_t count);

    // encoder-side setup: mirror init() from explicit params (FP-sensitive, compiled -fno-fast-math)
    void setup_for_encode(int mn, int mx, int dbits, int total,
                          uint32_t main_w, uint32_t main_iv, uint32_t main_od, int n_stages_,
                          double decay_, uint32_t fc_w, uint32_t fc_halve_k, uint32_t golomb_field,
                          const std::vector<int>& stage_orders, const std::vector<int>& stage_mu10,
                          const std::vector<uint8_t>& sched);

    // cascade internals
    void cascade_init();
    int  cascade_predict();
    void cascade_update(double actual);
};

// pred_type=3 stereo: OFR_PredictorStereo_Inner (main) + two cross-channel cascades.
struct OFR_CascadeStageX {        // cross-channel NLMS stage
    std::vector<float> w1, w2;    // this-channel taps, other-channel taps
    int size1, size2;
    double energy, mu, eps;
};
struct OFR_CascadeRing {
    std::vector<float> ring;      // 0x400 floats
    int cur, copy;               // head index, copy count (=max(size1+1,size2))
};
struct OFR_SubCascade {           // one channel's cascade (predicts via primary+secondary ring)
    int n_stages = 0;
    std::vector<OFR_CascadeStageX> stages;  // 1-based
    double bias = 0.0, decay = 1.0;
    std::vector<double> stage_pred, cumsum, errv;
    uint32_t fc_counter = 0, fc_last_halve = 0, fc_halve_interval = 0;
    int fc_size = 0; double fc_decay = 1.0;
    std::vector<double> fc_coefs, fc_v;
    std::vector<std::vector<double>> fc_mat;
};

class OFR_PredictorCascadeStereo {
public:
    OFR_PredictorStereo_Inner main;     // the +0x10 cross-channel LDLT predictor (reused)
    std::vector<OFR_CascadeRing> ringsA, ringsB;  // per-stage shared error rings (1-based)
    OFR_SubCascade casL, casR;          // L: primary A, secondary B ; R: primary B, secondary A

    int min_L=0, max_L=0, min_R=0, max_R=0, shift=0;
    int total_samples=0;
    uint32_t main_weight_param=0, main_max_order=0, main_right_order=0, main_interval=0;

    int seg_len=0, cc_count=0, mode_L=0, mode_R=0; uint32_t sched_idx=0;
    std::vector<uint8_t> schedL, schedR;
    bool need_init=false;
    uint32_t sample_counter=0;

    void init(OFR_RangeCoder* rc, uint32_t bit_depth, int Lmn,int Lmx,int Rmn,int Rmx, int dbits, int total);
    void decode(int32_t* dest, uint32_t count);
    void setup_for_encode(int Lmn,int Lmx,int Rmn,int Rmx, int dbits, int total,
                          uint32_t main_w, uint32_t main_iv, uint32_t main_maxord, uint32_t main_rightord,
                          int n_stages, double decay_, uint32_t fcw, uint32_t fch_k, uint32_t golomb_field,
                          const std::vector<int>& stage_size1, const std::vector<int>& stage_size2,
                          const std::vector<int>& stage_mu10,
                          const std::vector<uint8_t>& schL, const std::vector<uint8_t>& schR);
    void cascade_init();
    int  sub_predict(OFR_SubCascade& c, std::vector<OFR_CascadeRing>& pri, std::vector<OFR_CascadeRing>& sec);
    void sub_update(OFR_SubCascade& c, std::vector<OFR_CascadeRing>& pri, std::vector<OFR_CascadeRing>& sec, double actual);
    void fc_solve(OFR_SubCascade& c);
};

// entropy_type=3: "acm" advanced context-modeling entropy (preset max). All integer.
// See doc/ent3_analysis.md.
class OFR_EntropyAcm {
public:
    struct Ctx { std::vector<uint32_t> freqs; uint32_t num_symbols=0, total_freq=0, limit=0, num_nodes=0; };
    struct Chan {
        bool msb_flag=false;
        uint32_t reset=0, state=0, countdown=0;
        int64_t hist[8] = {0,0,0,0,0,0,0,0};
        std::vector<Ctx> bitlen;            // +0x58: 9 ctxs of 9 symbols
        std::vector<Ctx> vctx;              // +0x70: value contexts
        std::vector<int> vmap;              // +0x370: exponent -> ctx index
        std::vector<uint32_t> k;            // +0x3f0
        std::vector<uint32_t> nsym;         // +0x470
        std::vector<uint32_t> transform;    // +0x4f0
        std::vector<uint32_t> scale;        // +0x570
    };
    uint32_t bit_depth=0, channels=0, total_samples=0;
    std::vector<Chan> chans;
    bool needs_init=false;
    void init(OFR_RangeCoder* rc, uint32_t bd, uint32_t ch, uint32_t total);
    void decode_block(int32_t* dest, uint32_t count, OFR_RangeCoder* rc);
    uint32_t decode_sample(Chan& c, OFR_RangeCoder* rc);
};

class OFR_PostProcessor {
public:
    void init(OFR_RangeCoder* rc, uint32_t bit_depth, uint32_t channels);
    void init2(OFR_RangeCoder* rc, uint32_t channels, uint32_t bit_depth);   // post_type=2 (faithful, with value-remap)

public:
    uint32_t m_channels;

    int min_val_L;
    int max_val_L;
    int mult_L;
    int offset_L;

    int min_val_R;
    int max_val_R;
    int mult_R;
    int offset_R;

    int scaled_min_L;
    int scaled_max_L;
    int scaled_min_R;
    int scaled_max_R;

    bool flag8;

    // post_type=2 value-remap (tonal signals): per-channel dense-index -> original-value tables
    bool has_remap = false;
    std::vector<int32_t> remap_tbl[2];   // indexed by (decoded_value - remap_lo[ch])
    int remap_lo[2] = {0, 0};

    OFR_PostProcessor() : min_val_L(-0x800000), max_val_L(0x7fffff), mult_L(1), offset_L(0),
                          min_val_R(-0x800000), max_val_R(0x7fffff), mult_R(1), offset_R(0),
                          scaled_min_L(-0x800000), scaled_max_L(0x7fffff), scaled_min_R(-0x800000), scaled_max_R(0x7fffff),
                          flag8(true) {}

    void decode(int* dest, int count, int channels);
};


class OFR_BlockDecoder {
public:
    OFR_EntropyDecoder* entropy;
    OFR_Predictor* predictor;
    OFR_PredictorFastStereo* predictor_fast_stereo;
    OFR_PredictorStereo* predictor_stereo;
    OFR_PredictorCascadeMono* predictor_cascade_mono;
    OFR_PredictorCascadeStereo* predictor_cascade_stereo;
    OFR_EntropyAcm* entropy_acm;
    OFR_PostProcessor* post_processor;

    OFR_BlockDecoder() : entropy(nullptr), predictor(nullptr), predictor_fast_stereo(nullptr), predictor_stereo(nullptr), predictor_cascade_mono(nullptr), predictor_cascade_stereo(nullptr), entropy_acm(nullptr), post_processor(nullptr) {}

    void decode_block(uint32_t* dest, uint32_t count, OFR_RangeCoder* rc) {
        if (entropy) entropy->decode_block((int32_t*)dest, count, rc);
        if (entropy_acm) entropy_acm->decode_block((int32_t*)dest, count, rc);
        if (predictor) {
            predictor->decode((int*)dest, count);

        }
        if (predictor_fast_stereo) predictor_fast_stereo->decode((int*)dest, count);
        if (predictor_stereo) predictor_stereo->decode((int32_t*)dest, count);
        if (predictor_cascade_mono) predictor_cascade_mono->decode((int32_t*)dest, count);
        if (predictor_cascade_stereo) predictor_cascade_stereo->decode((int32_t*)dest, count);
        if (post_processor) post_processor->decode((int*)dest, count, post_processor->m_channels);
    }
};


class OFR_DecoderEngine {
public:
    OFR_DecoderEngine() : bitstream(nullptr), headData(nullptr), decode_buffer(nullptr), tailData(nullptr), corr_stream(nullptr) {}
    virtual ~OFR_DecoderEngine();
    virtual bool open(ReadInterfaceWrapper* wrapper);
    virtual uInt32_t read(void* dest, uInt32_t count);
    virtual bool seek(sInt64_t sample_pos);
    virtual bool readTail();
    virtual void close();

    uInt32_t block_decode(void* dest, uInt32_t count);

    OFR_BlockDecoder block_decoder;
    OFR_RangeCoder range_coder;
    OFR_BitStream* bitstream;

    sInt64_t total_samples; // 0x8
    uint8_t stream_stuff[0x48 - 0x10]; // 0x10
    void* headData; // 0x48
    uint32_t pred_val1, pred_val2, pred_val3;
    uInt32_t headSize; // 0x50
    uint32_t pad1; // 0x54
    void* tailData;
    int32_t* decode_buffer; // 0x58
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
    
    uInt32_t block_size;
    uInt32_t block_pos;
    sInt64_t samples_read_so_far;
    bool need_new_block;
    bool block_error;
    sInt64_t block_end_pos;
    void* corr_stream;
    // multi-block: track the current COMP payload so the range coder stays inside it and we
    // realign to the next COMP after the block (the range coder otherwise over-reads).
    uint32_t m_payload_start = 0;
    uint32_t m_payload_len = 0;
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
