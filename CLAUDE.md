# optimfrogcpp

C++ decompilation and rewrite of the OptimFROG decoder (closed-source).
Reference binary: `libOptimFROG.dylib` (v5.100, macOS x64) in the project root.
Part of the chisel ecosystem.

## Structure

```
src/
  main.cpp
  optimfrog.cpp                        — top-level decode pipeline
  optimfrog_decoder.cpp                — block decode orchestration
  optimfrog_decoder_entropy.cpp        — entropy decoding (RangeCoder, Huffman)
  optimfrog_decoder_predictor.cpp      — predictor (mono complete; stereo in progress)
include/
  OptimFROG.h                          — public API (from original header)
  OptimFROGDecoder.h                   — decoder interface
  optimfrog_decoder.h                  — internal decoder structs
  optimfrog_tables.h
  PortableTypes.h, SystemID.h
```

No CMakeLists.txt at time of move — build system uses the root Makefile.

## RE methodology

Analysis of `libOptimFROG.dylib` is done dynamically via LLDB Python scripts
(`test_lldb_*.py` in project root). Scripts extract internal variable state and
intermediate vectors during real binary execution, then compare against C++ output
sample-by-sample. Ghidra is also available for static RE (`libofr.asm`, `dump.asm`).

Reference files:
- `sine_mono.ofr` — mono test file (validated)
- `test_stereo.ofr` — stereo test file (176 KB, in progress)

## Current state (updated 2026-06-24 — stereo bit-exact)

### Working — verified bit-exact

- **Full mono pipeline**: Entropy + Predictor for mono files produces MD5-identical
  output to the reference binary on `sine_mono.ofr` (MD5: `4c820520d7faa9b2e11c76e9c4d2d7c7`).
- **LDLT solver (Cholesky)**: `solveCholeskyLeft` and `solveCholeskyRight` write the
  solution vector in-place onto the input array (`m_left_coefs`/`m_right_coefs`),
  using `m_temp_vector` only as scratchpad.
- **Predictor damping formula**: damping = `(W-1)/W` not `W`. Binary `FUN_00014dc0`
  receives `(-1.0 + W)/W`. Passing `W` caused exponential divergence at weight-update.
- **inst_matrix never updated**: `FUN_00014e30` does not write to inst_matrix after the
  initial setup at `sample_count == order+1`. Removed stale update in `update_weights()`.

- **Full stereo pipeline** (`test_stereo.ofr`, pred_type=1 / entropy_type=1 / post_type=2):
  bit-exact, MD5 `63c303b47bda85ba2a0029f71cfba1d5`. Uses `OFR_PredictorStereo` (integrated
  cross-channel LDLT, NOT a separate cc predictor) + the **fast stereo entropy** decoder.

### Stereo decode details (all verified against the binary)

- **Dispatch** (`FUN_00010870`): for our file the RC read order is PostProcessor(post_type=2,
  `FUN_00017c40`) → predictor `init`(`FUN_00007130`, reads weight/interval/order) → entropy
  `init`(`FUN_00004510`, reads one weight). `init_from_bitstream` on the stereo predictor is a
  genuine no-op — all RC reads happen in `init`.
- **Predictor min/max/shift** come from the post-processor via `FUN_00007400`; entropy/predictor
  `bit_depth` = `local_1c` = `FUN_00018d80(min,max)` (data bit-length), not `bitspersample`.
- **Fast stereo entropy** (`FUN_00004710`/`FUN_00005ef0`): per-channel variance, context array
  indexed by the **variance exponent** (`bits(var)>>52 - 1023`); `num_contexts = bit_depth*2`,
  per-context `num_symbols = bit_depth<4 ? 1<<bd : bd*8-0x10`; var update `var = sym²·w2 + w2 + var·w`.
  Initial variance = 1.0. This is a different decoder from the mono slow path (`FUN_00109ab0`).
- **Stereo decode loop** (`FUN_00007440`): clamp prediction to [min,max], `((clamped+res)<<shift)>>shift`,
  update with the final value. `lrint` rounding (cvtsd2si).

### Encoder (in progress)

Strategy: a lossless codec shares the adaptive models between encoder and decoder — the encoder
just runs them forward (`residual = x - clamp(predict())`, then `update(x)`, the SAME update as
decode). So REUSE the decoder predictor/entropy/context models and write only: (1) the range
*encoder* (dual of the range decoder), (2) header/COMP writer, (3) parameter selection. The chosen
params are written into the bitstream and read back by the decoder — they need NOT match the
reference encoder's choices, only be valid and self-consistent.

- **Range encoder DONE + verified** (`include/optimfrog_encoder.h`, `OFR_RangeEncoder`): exact dual
  of `OFR_RangeCoder`. 31-bit window (decoder's post-init range is 0x80000000 due to the 7-bit init +
  byte renorm at 0x800001), carry-counting byte emission (cache + run-of-0xFF), first emitted byte is
  the dummy `b0` the decoder ignores. Implements `encode_uniform`, `encode_bits`/`encode_bits_internal`
  (LSB-first 12-bit chunks), `encode_split`, `encode_symbol` (dual of `decode_symbol`: replicate the
  exact tree walk + freq +2 updates by navigating the path bits of `num_nodes+sym`), `encode_golomb`
  (note: this golomb never emits 1 — values are 0,2,3,4,…), `flush` (5× shift_low).
- **Round-trip test**: `src/enc_rt_test.cpp` — encode 300k mixed ops, decode with the real
  `OFR_RangeCoder` + parallel `OFR_ModelContext`, **0 mismatches**. Build:
  `clang++ -std=c++17 -O2 -Iinclude src/enc_rt_test.cpp build/liboptimfrogcpp_lib.a -o /tmp/enc_rt`.
- `ofr` (the official CLI encoder) imported into Ghidra (stripped, statically linked, ~107 fns found
  by auto-analysis — incomplete). Not the primary source; the decoder format IS the spec.

- **WORKING mono + stereo encoder** (`src/optimfrog_encoder.cpp`, `ofr_encode_mono16` /
  `ofr_encode_stereo16` + `ofrenc` CLI): 16-bit, pred=1 / ent=1 (fast) / post=1 (identity). Output is
  **bit-exact lossless** when decoded by BOTH our decoder and the **official reference decoder**
  (`/tmp/ref_gen`), verified on diverse signals (sine, music, white noise, full-range), mono AND stereo.
  Compression ≈ reference preset 0–2. Build: `cmake --build build` →
  `build/ofrenc in.raw out.ofr rate [channels]`.
  - **Stereo**: `total_samples` and COMP `numSamples` are in VALUES (frames×channels), channelConfig=1.
    Reuses `OFR_PredictorStereo_Inner` (predictLeft/updateLeft/predictRight/updateRight, lrint rounding,
    clamp per channel). The order **table index** sets BOTH max_order (`DAT_00326220`) and right_order
    (`DAT_00326200`); left_order = max-right. Fast stereo entropy = same contexts (shared) with two
    interleaved variances var_L/var_R. Stereo init reads weight via `bits(12)` (not read_12bit_value).
  - File layout (mirrors a real `ofr --raw` file, see hexdump RE): `OFR ` main(S=17) + `HEAD`(0) +
    `COMP` + `TAIL`(0). COMP = D(4) + **CRC32**(4, zlib over the D-4 bytes) + numSamples(4) + type(1)
    + cfg(1) + reserved(2 = ent<<11|pred<<6|post) + 1 encoderID byte (decoder skips it) + range stream
    (whose dummy `b0` doubles as the 2nd encoderID byte). D = 13 + len(range_out).
  - post=1 init dual: `split(16,min)`, `split(16,max)`, `bits(1,0)` (identity mult/offset). bit_depth
    here = bitspersample(16), NOT data_bits.
  - predictor init dual: `uniform(4096, W-2)` + interval **table index** `bits(3,idx)` + order **table
    index** `bits(5,idx)`. **Use table indices, NOT the escape** — the order/interval escape path
    desyncs vs the reference. Residual = `((sample - clamp(round(predict()))) << sh) >> sh` (wrap to
    data_bits, sh=(32-data_bits)&0x1f), then `update(sample)`. Reuses `OFR_Predictor`.
  - entropy init dual: `uniform(4096, w-2)` (read_12bit_value dual) → weight=(w-1)/w, weight2=1/w.
    Fast entropy (ent=1) encoder = exact dual of `fast_decode_sample`: zigzag(residual) →
    symbol/group/extra, tree walk + freq +2, var update. Contexts sized by data_bits.
  - **ent=2 (slow) encoder** (`SlowEntropyEncoder`, dual of `decode_one_sample`/`FUN_00109ab0`): 32-symbol
    master tree (reuse `OFR_ModelContext(32,0x8000)` + `encode_symbol`), `decode_abs_bits` dual ==
    `encode_bits`, the var-driven multi-branch raw_val split (fast `q=raw/uVar16`, bit-width
    `sym2=raw>>iVar14`, escape via `floor_log2`), variance starts at **0.0**. Verified mono+stereo vs
    reference. Both ent=1 and ent=2 selectable (the reference uses ent=2 for presets 0-3).
  - **pred=3 MONO encoder** (`OFR_PRED=3`): cascade NLMS. `OFR_PredictorCascadeMono::setup_for_encode`
    (in the -fno-fast-math pred3 TU) mirrors `init()` from explicit params; the forward loop (encoder TU,
    integer-only + calls into the -fno-fast-math cascade methods) mirrors `decode()`. Init dual writes
    main LPC params + n_stages + decay flag + fc weight/k + golomb flag + per-stage o5/mu10 + the
    schedule (encode_symbol on two 2-symbol contexts, N+1 syms, all-cascade). Verified bit-exact lossless
    vs reference (ent=1 and ent=2). Compression ≈ pred=1 with the current (untuned) fixed cascade params;
    beating pred=1 needs schedule/stage tuning (optimization, not correctness).
  - **pred=3 STEREO encoder** (`OFR_PRED=3` + stereo): `OFR_PredictorCascadeStereo::setup_for_encode`
    (pred3 TU) + forward loop (encoder TU, integer + cross-channel cascade method calls). Init dual:
    main params (order idx sets max+right_order) + n_stages + decay flag + fc weight/k + golomb flag +
    per-stage o5(sets size1/size2 via CASC_OT/CASC_S2)/mu10 + **two** interleaved schedules (4 contexts).
    Forward mirrors `decode()`: per frame L then R, each main(cross-channel LDLT) or cascade, shared
    rings A(L errs)/B(R errs). Verified bit-exact vs reference (ent=1/2, music/loud/noise stereo).
  - **MONO order up to ~96** (reliable, bit-exact vs reference). Since the faithful mono weight-update
    port (below), high-order mono decodes losslessly on the reference. Default order=64 ≈ preset 4
    (beats it on music). Orders ≥192 still desync (other high-order path, not chased). **STEREO still
    order ≤ 24** — the stereo Cholesky solver is not yet a faithful port.
- **Next encoder steps**: faithful stereo Cholesky port (unlocks high-order stereo + stereo ramp) →
  per-block param selection → ent=2/3 duals → pred=3 → 8/24-bit sample types.

## Test infrastructure (reference encoder available!)

`~/Downloads/OptimFROG_OSX_x64_5100/` has the official `ofr` CLI (x86_64, runs under Rosetta)
+ `SDK/Documentation/format.txt` (full format spec). Generate test files with any preset:
`ofr --encode --raw --channelconfig {MONO|STEREO_LR} --sampletype SINT16 --rate 44100 --preset N in.raw --output x.ofr`.
Reference decode via `/tmp/ref_gen` (compile `ref_gen.c` with `-arch x86_64 ... libOptimFROG.0.dylib`).
The COMP block's "2 bytes reserved" word encodes `pred=(w>>6)&0x1f, ent=w>>11, post=w&0x3f`.

## Test status (preset matrix, 16-bit, realistic non-tonal sources)

**ALL standard presets bit-exact, mono+stereo (0-10 AND max): 24/24.**

| Combo | Status |
|---|---|
| pred=1, ent∈{1,2}, post∈{1,2}, mono+stereo (presets 0-3) | PASS bit-exact |
| pred=3 mono+stereo (presets 4-10) | PASS bit-exact (cascade NLMS, `optimfrog_decoder_pred3.cpp`) |
| entropy_type=3 "acm" (preset max) mono+stereo | PASS bit-exact (`optimfrog_decoder_ent3.cpp`) |

### entropy_type=3 note (acm, all integer)
Context-modeling entropy: 9×9 Markov over bit-length "states" (per-channel +0x58) + value contexts
selected by the exponent of an 8-tap exponentially-smoothed magnitude (`hist[i] += (v<<16 - hist[i])>>(3+i)`).
Per-context k/num_symbols/scale read at init (`FUN_00004c00`); per-sample decode `FUN_00006100`.
Reuses the same adaptive-tree contexts as the fast entropy. See `doc/ent3_analysis.md`.

### pred_type=3 stereo note
Reuses `OFR_PredictorStereo_Inner` (main) + two **cross-channel** cascades sharing two per-stage error
rings (A=L errors, B=R errors); L cascade primary=A/secondary=B, R cascade primary=B/secondary=A.
The final combiner's initial `last_halve` = the golomb param field (0x67284), so the first LDLT re-solve
fires at counter = halve_interval + that field (NOT at halve_interval). Same field exists for mono (0x437ac).

### pred_type=3 mono notes (bit-exact)
- Dual predictor: LPC (reuses `OFR_Predictor`) + cascade NLMS, alternating per segment via an
  entropy-coded schedule. See `doc/pred3_analysis.md`.
- **Float32 bit-exactness was the hard part** (TU compiled `-fno-fast-math -ffp-contract=off`):
  the stage FIR must match the SSE accumulation grouping EXACTLY — per iteration
  `acc = h[j+4]*w[j+4] + (h[j]*w[j] + acc)`, and the horizontal sum is pairwise
  `((a8+a7)+(a6+a5))` with each lane promoted to double. Stage energy is a **double** (the
  Ghidra `(long)dVar3` was a misread; the asm uses MOVSD). Schedule decode reads N+1 symbols
  (off-by-one matters — RC desync otherwise).
- Final combiner = LDLT-solved LPC over cascade cumulative outputs (`FUN_00015890` builds
  exp-weighted covariance M + cross-corr V; `FUN_00015570` periodically re-solves via `FUN_000152c0`,
  an LDLT with row stride 8, diag threshold 2^-17, energy gate V[0]>=0.5).

post_type=2 value-remap table (`FUN_00017f00`/`FUN_000189f0`, `optimfrog_decoder_post2.cpp`):
**implemented**. Active only when the per-channel transform flag is set (tonal/synthetic signals).

## Decoder coverage — 100% (mono AND stereo, including all pathological signals)

**The decoder is now bit-exact on EVERYTHING.** The perfect-ramp singular-covariance gap — long the
only documented un-closed case — is **CLOSED for both mono and stereo**:
- mono ramp/silence/square: 12/12 each; stereo ramp/silence/square: 12/12 each
- 24/24 real + 24/24 sine (mono+stereo), sine_mono + test_stereo MD5 intact

How it was closed (the recipe — the weight-update/solver must be bit-for-bit faithful AND not
reordered by the compiler):
1. **`optimfrog_decoder_predictor.cpp` compiled `-fno-fast-math -ffp-contract=off`** (CMakeLists) — the
   binary is -fno-fast-math; without this the careful op-ordering gets reordered and near-singular
   covariances diverge by 1 ULP. THIS was the missing piece on all prior attempts.
2. **Mono** (`FUN_00014e30`+`FUN_00012ca0`): `solve_ldlt` = exact `FUN_00012ca0` port (in-place stride,
   `m[i][k]*diag[k]*m[j][k]` inner order, separate `d -= m[i][k]²*diag[k]` loop on the stored
   normalized value, threshold 2^-17); `update_weights` damp_pow via **exp-by-squaring** (not std::pow),
   energy gate `R[0] >= 0.5` (DAT_19770).
3. **Stereo** (`FUN_00013310`/`FUN_00013bf0` → `FUN_00012ca0`): `OFR_SolveLDLT` threshold **2^-17**
   (was 1e-15) and `solveCholeskyLeft/Right` energy gate **0.5** (was 1e-7). Those two constants +
   the no-fast-math compile closed stereo ramp/square (the binary's two-stage singular fallback in
   `FUN_00013310` is NOT needed in practice — correct constants alone suffice).

## Next work (ordered by cost)

1. **Encoder** (in progress) — mono high-order done; next stereo high-order (still order≤24; the
   high-order stereo path desyncs — investigate), ent=2/3 duals, pred=3, 8/24-bit.

## Recent fixes (this session)
- entropy path selection by `type==1 && channels==2` (was `is_fast_stereo`) → ent=2 stereo works.
- `OFR_PostProcessor::m_channels` was never set → set it in `init`.
- `data_bits = ofr_bitlen(min,max)` computed for mono too (was hardcoded 13) → entropy depth correct.
- **mono predictor decode** rewritten to match `FUN_00006f80`: clamp prediction to [min,max], apply
  `((clamp+err)<<shift)>>shift`, update with the final value. min/max/shift set from post-processor.
  (The old version was identity — only worked when no clamp/shift was needed, e.g. sine_mono.)

## Workflow rules

- No `git commit` / `git push` without explicit request
- Comments: sparse, English, lowercase inside functions
- Use `rtk ls` instead of `ls`
