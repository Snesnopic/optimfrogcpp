// round-trip test: encode a symbol stream with OFR_RangeEncoder, decode with OFR_RangeCoder.
#include "../include/optimfrog_decoder.h"
#include "../include/optimfrog_encoder.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

struct MemReader {
    const uint8_t* data;
    uint32_t size;
    uint32_t pos;
};
static sInt32_t mem_read(void* inst, void* dst, uInt32_t count) {
    MemReader* m = (MemReader*)inst;
    uint32_t n = count;
    if (m->pos + n > m->size) n = m->size - m->pos;
    memcpy(dst, m->data + m->pos, n);
    m->pos += n;
    return (sInt32_t)n;
}

enum OpKind { OP_UNIFORM, OP_BITS, OP_SPLIT, OP_SYMBOL, OP_GOLOMB };
struct Op { OpKind kind; uint32_t a; uint32_t v; };

int main() {
    srand(1234);
    const uint32_t NSYM = 37;
    std::vector<Op> ops;
    for (int i = 0; i < 300000; i++) {
        int k = rand() % 5;
        if (k == 0) { uint32_t N = 2 + rand() % 60000; ops.push_back({OP_UNIFORM, N, (uint32_t)(rand() % N)}); }
        else if (k == 1) { uint32_t b = 1 + rand() % 20; ops.push_back({OP_BITS, b, (uint32_t)(rand() & ((b>=32)?0xffffffff:((1u<<b)-1)))}); }
        else if (k == 2) { uint32_t b = 1 + rand() % 30; ops.push_back({OP_SPLIT, b, (uint32_t)(rand() & ((b>=32)?0xffffffff:((1u<<b)-1)))}); }
        else if (k == 3) { ops.push_back({OP_SYMBOL, 0, (uint32_t)(rand() % NSYM)}); }
        else { uint32_t gv = rand() % 2 ? 0 : (2 + rand() % 100000); ops.push_back({OP_GOLOMB, 0, gv}); }
    }

    OFR_ModelContext enc_ctx; enc_ctx.init(NSYM, 0x2000);
    OFR_RangeEncoder enc;
    for (auto& op : ops) {
        if (op.kind == OP_UNIFORM) enc.encode_uniform(op.a, op.v);
        else if (op.kind == OP_BITS) enc.encode_bits(op.a, op.v);
        else if (op.kind == OP_SPLIT) enc.encode_split(op.a, op.v);
        else if (op.kind == OP_SYMBOL) enc.encode_symbol(enc_ctx, op.v);
        else enc.encode_golomb(op.v);
    }
    enc.flush();

    MemReader mr{enc.out.data(), (uint32_t)enc.out.size(), 0};
    ReadInterface ri{}; ri.read = mem_read;
    ReadInterfaceWrapper w{&ri, &mr};
    OFR_BitStream bs(&w);
    OFR_RangeCoder rc;
    rc.init(&bs);
    OFR_ModelContext dec_ctx; dec_ctx.init(NSYM, 0x2000);

    int fails = 0;
    for (size_t i = 0; i < ops.size(); i++) {
        Op& op = ops[i];
        uint32_t got;
        if (op.kind == OP_UNIFORM) got = rc.decode_uniform(op.a);
        else if (op.kind == OP_BITS) got = rc.read_uniform_bits(op.a);
        else if (op.kind == OP_SPLIT) got = rc.read_uniform_split(op.a);
        else if (op.kind == OP_SYMBOL) got = rc.decode_symbol(dec_ctx);
        else got = rc.read_golomb();
        if (got != op.v) {
            if (fails < 10) printf("MISMATCH op#%zu kind=%d a=%u expected=%u got=%u\n", i, op.kind, op.a, op.v, got);
            fails++;
        }
    }
    printf("ops=%zu bytes=%zu fails=%d\n", ops.size(), enc.out.size(), fails);
    return fails ? 1 : 0;
}
