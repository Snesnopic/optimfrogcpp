# entropy_type=3 — "acm" advanced context-modeling entropy (preset max)

All-INTEGER decoder (no float). Object = engine+0xb8 (param_1[0x17]); vtable[0]=init FUN_00004c00,
vtable[1]=block-decode FUN_00005480, per-sample core FUN_00006100. Setup FUN_00004be0 sets
obj+0x2f48=totalSamples, +0x2f4c=bit_depth (num value-contexts), +0x2f50=channels, +0x2f54=decoded.

Per-channel state block: base = obj + ch*0x5e8. Fields (relative to base):
- +0x08 (char) msb_flag
- +0x0c (u32) countdown reset (run length of a bit-length block)
- +0x10 (u32) state = previous bit-length symbol (0..8)
- +0x18 .. +0x50 : 8× int64 exponentially-smoothed magnitude histories (shift 3..10)
- +0x58 (ptr)  bit-length Markov context array = FUN_0000fb90(base+0x58, 9, 9, 0x8000) — 9 ctxs × 9 syms
- +0x68 (u32) countdown
- +0x70 + c*0x18 : value contexts (FastCtx, c in 0..bit_depth-1), built by FUN_00002bd0(ctx, num_syms, scale)
- +0x370 + e*4 : map magnitude-exponent e → value-context index
- +0x3f0 + c*4 : per-context "k" (golomb base, = read8+1)
- +0x470 + c*4 : per-context num_symbols (= read7+2)
- +0x4f0 + c*4 : per-context transform type (1 or 2)
- +0x570 + c*4 : per-context scale (1<<(read3+12))

## decode FUN_00005480 (block)
remaining = min(req, total-decoded); assert remaining % channels == 0.
loop samples (step channels): for each ch: dest[i+ch] = FUN_00006100(obj, ch, rc). update decoded.

## per-sample FUN_00006100(obj, ch, rc)
base = obj + ch*0x5e8.
1. if countdown(+0x68)==0:
   decode state symbol from bitlen ctx array[+0x58][ state(+0x10) ] (range-tree, 9 syms).
   new_state = sym; +0x10 = new_state; countdown = reset(+0x0c).
   if new_state==8: zero the 8 histories; countdown--; return 0.
   countdown--.
   else: countdown--; if state(+0x10)==8 return 0.
2. value decode:
   mag = hist[ +0x18 + state*8 ]; if (msb_flag && state!=0 && hist[state-1] > mag) mag = hist[state-1].
   e = bitlen(mag>>16) using the (>>16,>>8,>>4,>>2) ladder → index.
   ctx = map[+0x370][e]; pred = (scale[+0x3f0 region? actually +0x3f0=k]...). 
     Actually: uVar13 = (u32)((u64)(*+0x3f0[ctx]) * mag >> 0x18); pred = uVar13 + 1.   [+0x3f0[ctx] = scale]
   decode sym from value ctx +0x70+ctx*0x18 (range tree).
   sym2 = node - num_nodes.
   if sym2 == num_symbols[+0x470][ctx]-1:  // ESCAPE (top symbol)
     v = pred * sym2; ... bitlen(v) → bits; rem = bit_depth - bits; decode index in [0,rem); 
     low = FUN_000055f0(rc, idx+bits); value = (1<<(idx+bits)) + low.
   else:
     if pred < 0x4001: decode uniform residual r in [0, pred); value = sym2*pred + r.
     else: decode r via the (pred+0x4000>>14) two-step scheme; value = sym2*pred + r.
3. update 8 histories: lVar15 = value<<16; for i in 0..7: hist[i] += (lVar15 - hist[i]) >> (3+i).
4. return zigzag(value) = (value&1)? ~(value>>1) : (value>>1).

NOTE the +0x68==0 path ALSO does a first value-context decode (the big first block) before falling
through — re-check: the iVar24==0 branch decodes the STATE, then continues to the value decode below
(shared). The uVar19==8 path returns 0 early.

## init FUN_00004c00 (reads from rc)
per channel ch (0..channels-1):
- read 1 bit → msb_flag (+0x08); (range split half)
- read 4-bit idx → reset(+0x0c) = 1<<((idx>>1)+6), +half if idx odd.
- read the per-"first-context" params: a 1-bit then (if set) more → local_40 (transform type 1/2),
  local_44 = 1<<(read3 + 12) (scale), local_48 = read8+1 (k), iVar13 = read7+2 (num_symbols).
  (if the 1-bit was 0: all zero / num_symbols path skipped → trivial context.)
- per value-context loop (e = 0..bit_depth-1): a 1-bit "same as first?" ; if same reuse local_* ;
  else read its own transform(1bit→1/2), scale(1<<(read3+12)), k(read8+1), num_symbols(read7+2).
  Build context: FUN_00002bd0(base+0x70+ctxidx*0x18, num_symbols, scale-as-limit). map[+0x370][e]=ctxidx.
  Store +0x4f0=transform, +0x570=scale, +0x3f0=k, +0x470=num_symbols.
- zero histories (+0x18..+0x50); FUN_0000fb90(base+0x58, 9, 9, 0x8000); countdown=0; state=0.
obj+0x2f54 (decoded) = 0.

Reuse: FastCtx (=FUN_00002bd0/df0/ea0 tree), FUN_0000fb90 (ctx array), FUN_000055f0 (read bits),
all already ported in optimfrog_decoder_entropy.cpp / pred3. Verify init params via LLDB on m_*_pmax.ofr.
