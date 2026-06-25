# pred_type=3 — cascade NLMS predictor (presets 4→max)

Full RE map of the high-compression predictor. Mono object base offsets shown (param_1[0x11]);
stereo (param_1[0x12]) mirrors at 0x67xxx with two channels + cc alternation.

## Object layout (mono pred=3, base = object)
- `+0x10`   : standard LPC predictor (same struct/funcs as pred=1: predict `FUN_00014ce0`, update `FUN_00015100`, init `FUN_00014dc0`)
- `+0x42c60`: secondary **cascade NLMS** predictor (predict `FUN_0000edc0`, update `FUN_0000eee0`, init `FUN_0000ece0`)
- `+0x43680`: main LPC weight_param   `+0x43684`: main order   `+0x43688`: main interval
- `+0x4368c`/`0x43690`: min/max   `+0x43694`: shift (32-databits)   `+0x43698`: progress counter (0xac44)
- `+0x4369c`: need_init flag   `+0x436a0`: progress cb   `+0x436a8`: total block samples
- `+0x436ac`: cc segment length (= main interval)   `+0x436b0`: current mode (0=LPC,1=cascade)
- `+0x436b4`: cc countdown   `+0x436b8`: schedule index   `+0x437b8[]`: per-segment type schedule (bytes)
- cascade param block (read by init, consumed by `FUN_0000ece0`):
  - `+0x437a0`: nStages (+1)   `+0x437a4`: cascade final weight_param   `+0x437a8`: 1<<k size
  - `+0x437ac`: golomb count   `+0x43798`: a double scale (0x436c0-area mu's)   `+0x43750+i*4`: per-stage order
  - `+0x436c0+i*8`: per-stage mu (double)   `+0x43708+i*8`: per-stage clamp
  - NOTE: `FUN_0000ece0` reads its inputs from a param struct at object+0x436c0 region (offsets 0xd8/0xe0/0xe4/0xe8/0xec, and per-stage 0x8/0x48/0x90).

## Decode loop (`FUN_00008590` mono)
On need_init: init main LPC (`FUN_00014dc0`), init cascade (`FUN_0000ece0`), counter=0xac44.
Per sample i:
- progress countdown.
- if cc countdown(0x436b4)==1: set a flag 0x42c50 from schedule[idx].
- mode = 0x436b0:
  - mode 0 (LPC): p=round(FUN_00014ce0); clamp[min,max]; out=((clamp+res)<<sh)>>sh; update main(out);
    then `FUN_0000edc0(cc)` (advance cascade), `FUN_0000eee0(out, cc)` (train cascade on out).
  - mode 1 (cascade): p=FUN_0000edc0(cc) (returns int already rounded); clamp; out=((clamp+res)<<sh)>>sh;
    `FUN_0000eee0(out, cc)`; update main(out).
- cc countdown--; on 0: mode=schedule[idx], countdown=0x436ac, idx++.

## Cascade predict `FUN_0000edc0(cc)` → int
cc has N stages (count at cc+0xa18). cumulative array at cc+0x9c8.
For stage s=1..N: advance ring (cc+0x850+s*0x18), zero new slot, stagePred=`FUN_0000f850`(weights cc+0x6e8+s*0x28, hist);
  store at cc+0x938+s*8; cumsum[s] = stagePred + cumsum[s-1].
Final = `FUN_000154e0`(cc, &cumsum[N+1]) + bias(cc+0x928). Return round(final).

## Cascade update `FUN_0000eee0(actual, cc)`
err = actual - bias(cc+0x928); bias = actual * decay(cc+0x930).  (cc+0x980 = err)
For stage s=1..N: err -= stagePred[s] (cc+0x938+s*8); store cc+0x980+s*8;
  `FUN_0000f8c0`(err_float, stage cc+0x6e8+s*0x28, ringptr cc+0x850+s*0x18)  ← NLMS weight update;
  ring head = prev err (cc+0x980+(s-1)*8).
cumsum-tail = err; `FUN_00015890`(cc, &cc+0x9d0+N*8)  ← final combiner update.

## Stage predict `FUN_0000f850(stage, histptr)` → double (FIR dot, float32)
size=stage[1]; w=stage[0]; h=histptr - size*4(float). sum over i: h[i]*w[i], unrolled ×8 (4 accumulators).
Returns (double)acc8+acc7+acc6+acc5.  ALL FLOAT32 multiply/add.

## Stage NLMS update `FUN_0000f8c0(res_float, stage, histptr)`
stage[0]=weights, [1]=size, [2]=energy(double via long bits), [3]=mu(double), [4]=eps(double).
energy = h[-1]² + (energy - h[~size]²)   (sliding window energy, float² → double)
step = (res * mu) / (energy + eps)    (double, then →float)
for i in 0..size: w[i] += hist[i]*step   (float32, unrolled ×8).

## Final combiner predict `FUN_000154e0(cc, &cumsum_end)` → double
size=cc[2](int at cc+8); coefs at cc+0x268. dot: sum cumsum[-(i+1)] * coefs[i], double.

## Final combiner update `FUN_00015890(cc, dptr)`  (NLMS over the cumsum history, double)
cc+0 = counter++; cc+2=size; cc+6=decay; cc+8.. inner coef matrix; halve via FUN_00015570 when counter exceeds.

## Init `FUN_000077d0` (vtable[0], reads from RC = param_2)
1. main LPC: weight(12b, 0xfff→16b+0x1001 else +2), interval(3b, table DAT_21ca0 / 7→16b+1), order(5b, table DAT_21cd0 / 0x1f→8b+1). → 0x43680/0x43688/0x43684.
2. cascade: nStages=3b+1 (0x437a0); 0x43798 = read (1b flag; if set 10b/DAT_19718 scale else 1.0);
   0x437a4=12b weight+2; 0x437a8=1<<(3b); 0x437ac=golomb-ish (1b; if set 16b);
   per stage s=1..nStages: order = 5b (table DAT_21ea2*4+4 / 0x1f→8b*4+4) → 0x43750+s*4;
     mu = 10b / DAT_19688 → 0x436c0+s*8.
3. 0x436ac = main interval. per-stage clamp init at 0x43708 (from |min|,|max|).
4. SCHEDULE: a separate adaptive entropy ctx (local_40, FUN_0000fb90(.,2,2,0x8000)) decodes the
   per-segment type array into 0x437b8[]: nSegments = (totalSamples + ~min(order+1,total) + interval)/interval.
   First entry decoded with a 2-symbol ctx; rest in a loop. 0x437b8[nSeg]=1 (sentinel).
5. 0x436b4 = min(order+1, total); 0x436b0 = schedule[0]; 0x436b8=1; 0x4369c=1.

## Implementation risks
- **float32 exactness**: stages use float multiply/accumulate with SSE ×8 unrolling. `-ffast-math` will
  break this (contraction/reordering). Compile this TU without fast-math; replicate the 4-accumulator
  unrolled order exactly; use `float` not double for stage ops.
- The embedded entropy-coded schedule must be decoded with the exact same adaptive tree as the binary
  (FUN_0000fb90/FUN_00002df0 — same family already ported for fast-stereo entropy contexts).
- Stereo pred=3 adds L/R + cc cross-channel alternation on top (object 0x67xxx).

## Constants / tables (from .data)
- `DAT_19688 = 1000.0`  → per-stage mu = read10(idx in [0,1023]) / 1000.0
- `DAT_19718 = 1/1024 (2^-10)` → 0x43798 scale = read10(idx)/1024 (only if its 1-bit flag set; else 1.0)
- `DAT_21ea2[]` (cascade stage order table, shorts): {512,256,128,64,128,64,32,16,0,...}; order = table[idx5]*4+4 (idx5==0x1f → read8*4+4)
- interval table `DAT_21ca0` and order table `DAT_21cd0` = same as pred=1 (`DAT_00326238`/`DAT_00326220` in our code)
- `FUN_00016830` = read 16-bit; `FUN_00016760` = read 8-bit (already have as read_uniform_bits(16/8))
- ring init `FUN_0000f760(ring, order+1, 0x400)`: ring[+0x10]=order+1(copycount), [+0x14]=0x400(size),
  alloc 0x400 floats zeroed [0..order+1], cur ptr = base+(order+1).
- stage alloc `FUN_0000e850(mu?, eps?, stage, order)`: weights = order floats (16-aligned, min 8), zeroed.
- final init `FUN_00015830(weight,cc,size,?,?)`: bzero 0x6e8; cc+8=size, cc+0xc, cc+0x18=weight(decay), cc+0x268=1.0.

## Stereo pred=3 (FUN_00009c20 decode, FUN_00009be0 setup, object 0x67xxx) — TODO
Mirrors mono but: main predictor at +0x10 is `OFR_PredictorStereo_Inner` (the pred=1 stereo
cross-channel LDLT, already bit-exact); secondary cascade at +0x65c80 is a **cross-channel**
cascade with TWO channels + TWO schedules (L at +0x67290, R at +0x7b690).
- per frame: decode L (mode +0x67188, predictLeft FUN_00012ed0 / cascade-L FUN_0000f2d0 predict,
  FUN_0000f400 update; clamp [0x67158,0x6715c]), then R (mode +0x6718c, predictRight FUN_00012f90 /
  cascade-R FUN_0000f510, FUN_0000f640; clamp [0x67160,0x67164]). Shared countdown +0x67190,
  reset +0x67184, sched idx +0x67194.
- cascade init FUN_0000f130 (reads param block at +0x67198).
- cascade-L predict FUN_0000f2d0: SAME shape as mono FUN_0000edc0 but stage stride 0x30 (not 0x28),
  offsets: stages count +0x14c0, cumsum +0x1390, ring +0x1130, stage +0xdd0, stage_pred +0x1300,
  bias +0x12e0. Per-stage predict is **FUN_0000f980 (3 args: stage, ringptr, +0x1208+s*0x18)** — the
  extra arg is the OTHER channel's history → cross-channel FIR. Final combiner reuses FUN_000154e0.
### Stereo cascade — full map (all functions decompiled, ready to port)
**Two shared error rings**: chA at +0x1130 (L errors), chB at +0x1208 (R errors), each a ring struct
(cur,base,copy,size) like mono. **Two sub-cascades**:
- L cascade: stages +0xdd0+s*0x30, combiner at obj+0 (FUN_000154e0/FUN_00015890), bias +0x12e0,
  decay +0x12e8, stage_pred +0x1300, cumsum +0x1390. predict FUN_0000f2d0 (advances chA, secondary=chB),
  update FUN_0000f400. primary ring chA, secondary chB.
- R cascade: stages +0xf80+s*0x30, combiner at obj+0x6e8, bias +0x12f0, decay +0x12f8,
  stage_pred +0x13e0, cumsum +0x1470. predict FUN_0000f510 (advances chB, secondary=chA), update FUN_0000f640.

**Cross-channel stage** (0x30 bytes): +0 w1, +8 w2, +0x10 size1, +0x14 size2, +0x18 energy(double),
  +0x20 mu, +0x28 eps. init FUN_0000ea10(mu,eps,stage,size1,size2) allocs w1[size1],w2[size2] (16-aligned, min8, zeroed).
- FIR FUN_0000f980(stage, primary_ring_cur, secondary_ring_cur): acc=0; loop1 over size1 on
  primary[cur-size1..cur-1] with w1; loop2 over size2 on secondary[cur+1-size2..cur] with w2 — SAME 4-acc
  SSE grouping as mono `acc=h4*w4+(h0*w0+acc)`, accumulators CARRY across both loops; pairwise horizontal sum.
  NOTE secondary window is `secptr + (1-size2) .. secptr` (INCLUDES secptr[0]); primary is `pri-size1..pri-1`.
- NLMS FUN_0000fa30(res_float, stage, primary_ring_cur, secondary_ring_cur): energy =
  secptr[0]^2 + ((priptr[-1]^2 + (energy - priptr[-(size1+1)]^2)) - secptr[-size2]^2) (double, MOVSD);
  step=(res*mu)/(energy+eps); w1[i]+=pri[i]*step (i in 0..size1-1, pri=priptr-size1);
  w2[i]+=sec[i]*step (i in 0..size2-1, sec=secptr+1-size2).

**Decode FUN_00009c20** per frame (i+=2): L then R.
- L: mode +0x67188; mode0 → predictLeft(FUN_00012ed0) clamp[+0x67158,+0x6715c] shift+0x67168, updateLeft, then
  cascadeL predict(FUN_0000f2d0)+update(FUN_0000f400); mode1 → cascadeL predict (returns int) clamp/shift,
  cascadeL update, updateLeft. R: mode +0x6718c; clamp[+0x67160,+0x67164]; predictRight/updateRight +
  cascadeR (FUN_0000f510/FUN_0000f640).
- countdown +0x67190 (reset +0x67184); on hitting 0: L mode=schedL[idx], R mode=schedR[idx],
  count=+0x67184, idx++. When count==1, sets flags +0x65c70/+0x65c71 from schedL/R[idx] (look-ahead, used by cascade? verify).

**Init FUN_00008b00** (vtable[0]): reads, in order:
1. main stereo predictor: weight(12b → +0x67148), interval(3b/DAT_21ca0 → +0x67154),
   order5b: max_order(DAT_21cd0 → +0x6714c) + right_order(DAT_21cf0 → +0x67150) [0x1f→8b+1, 8b].
2. cascade: nStages(3b+1 → +0x67278); decay(1b; set→10b*DAT_19718 else 1.0 → +0x67270);
   fc weight(12b → +0x6727c); fc halve(3b→1<<k → +0x67280); +0x67284(1b; set→16b else 0x40).
   per stage s: size1(5b → DAT_21ea2[idx] / 0x1f→8b*4+4 → +0x67228+s*4),
   size2(DAT_21eaa[idx] / 0x1f→8b*4 → +0x6724c+s*4), mu(10b/DAT_19688 → +0x67198+s*8).
   +0x67184 = interval. stage eps +0x671e0 = max(|Lmin|,|Lmax|,|Rmin|,|Rmax|,1.0).
3. TWO schedules (interleaved): ctxL[2] & ctxR[2] (FUN_0000fb90(.,2,2,0x8000)); first L sym → +0x67290[0],
   first R sym → +0x7b690[0]; then loop: nseg = ((total>>1) + ~min(max_order-right_order+1, total>>1) +
   interval)/interval; per seg decode L (ctxL[prevL]) and R (ctxR[prevR]) into +0x67290[i]/+0x7b690[i].
   sentinels at [nseg+1]. total = +0x67180 (interleaved samples).
4. +0x67190=cc_count=min(max_order-right_order+1, total>>1), +0x67188=schedL[0], +0x6718c=schedR[0],
   +0x67194=1, +0x67170=need_init.

Tables: DAT_21ea2 (size1) = {512,256,128,64,128,64,32,16,0..}; DAT_21eaa (size2) = {128,64,32,16,0..}
(= DAT_21ea2 shifted by 4 shorts). DAT_21cf0 = right_order table (pred=1 stereo, = our DAT_00326200).

Reuse: OFR_PredictorStereo_Inner (done) for main; mono cascade machinery extended with cross-channel FIR/NLMS.

## Helper functions to port
predict: FUN_0000edc0, FUN_0000f850, FUN_000154e0
update:  FUN_0000eee0, FUN_0000f8c0, FUN_00015890, FUN_00015570(halve)
init:    FUN_000077d0(params+schedule), FUN_0000ece0(cascade), FUN_0000e850(stage alloc), FUN_0000f760(ring), FUN_00015830(final)
context: FUN_0000fb90/FUN_00002bd0/FUN_00002df0/FUN_00002ea0 (already have equivalents in entropy fast path)
