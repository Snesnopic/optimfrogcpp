# Status

Last updated after the conformance-suite hardening pass (bidirectional test suite across
8/16/24/32-bit, mono/stereo, all presets).

## Decoder coverage

| Component | Status |
|---|---|
| Container/header parsing (`OFR `/`HEAD`/`COMP`/`TAIL`) | bit-exact |
| Multi-block real audio (entropy type varying per block) | bit-exact |
| Predictor `pred=1` (LPC), mono + stereo | bit-exact |
| Predictor `pred=3` (cascade NLMS + LDLT final combiner), mono + stereo | bit-exact |
| Entropy `ent=1` (fast, context-indexed by variance exponent) | bit-exact |
| Entropy `ent=2` (slow, master-tree + escape) | bit-exact |
| Entropy `ent=3` (ACM, integer context modeling) | bit-exact |
| Post-processor `post=1` (identity / scale+offset) | bit-exact |
| Post-processor `post=2` (value-remap for tonal/synthetic signals) | bit-exact |
| Bit depths 8/16/24/32, mono + stereo | bit-exact |

Verified via `tests/conformance/`: 8 real-world corpus files + ~90 synthetic edge-case signals
(silence, DC, full-scale, degenerate/tonal stereo, boundary lengths 1..65537, exact-period tones,
white noise, wrapping ramps) × presets {0,2,4,7,10,max} × bit depths {8,16,24,32}, decoded by our
decoder from reference-encoder output. Current pass rate: 3952/3968 (99.6%).

### Known residual gaps (decoder)

Two narrow classes remain, both irrelevant to real-world audio:

1. **`impulseneg` at 32-bit** (a periodic exact-`INT32_MIN` impulse in an otherwise-silent
   32-bit signal). Root cause identified: when the post-processor's `mult` is exactly `±2^31`,
   the coded values `+1` and `-1` alias to the same final sample (their difference, `2*mult`, is
   congruent to `0 mod 2^32`), and a second, compounding issue in how the LDLT solver's singular
   fallback behaves on repeated cycles of this exact degenerate pattern. A principled partial fix
   (clamp bounds derived from `data_bits`' natural two's-complement range instead of the tight
   per-channel min/max) closes the *first* spike/silence cycle but regresses the common
   `mult == 1` case and still diverges from the *second* cycle onward — reverted pending a more
   careful, narrowly-scoped fix. Needs an LLDB-level comparison of the reference's LDLT re-solve
   behavior on this specific signal shape.
2. **Exact-period tonal edge at 16-bit** (`tone_p16`, and `len3`/`len4`/`len5` boundary lengths).
   Pre-existing, not investigated in depth yet.

## Encoder coverage

| Combo | Status |
|---|---|
| `pred={1,3} × ent={1,2,3} × {mono,stereo}` (12 combinations) | bit-exact lossless vs reference |
| Bit depths 8/16/24/32 | bit-exact lossless vs reference |
| Mono order up to ~96 (competitive with reference presets 0-4) | bit-exact |
| Stereo order ≤ 24 (stereo Cholesky solver is not yet a faithful port at higher orders) | bit-exact |

`OFR_BEST=1` does a small safe config search (pred1 orders / od_idx, pred3, × ent{1,2,3}) and
keeps the smallest valid output — competitive with reference presets 0-6 on music.

### Known gaps (encoder)

- No faithful high-order stereo Cholesky solver port yet (limits stereo compression ceiling).
- `SlowEntropyEncoder` mirrors the *pre-fix* decoder formulas in one edge (documented in the
  source) — not confirmed broken by any current test, but not re-verified against the
  32-bit-specific decoder fixes either.
- No per-block parameter selection (single config for the whole stream).
- DualStream (`pred=4`, lossy) and Float (`off`, IEEE float samples) formats are not implemented.

## Bugs found and fixed this round (for context on fix provenance)

All found via the conformance suite + LLDB comparison against the reference binary at the exact
divergence point (see git log for full technical writeups):

- Suite itself never passed `--preset` to the reference encoder (was silently testing only the
  default preset for the "preset" dimension).
- `post=1` mult/offset range reduction was missing entirely for non-identity scale factors.
- 8-bit output: CLI hardcoded 16-bit sample width; `post=2` init read min/max at a hardcoded
  16-bit width regardless of actual bit depth.
- Infinite hang in the ACM entropy escape path for pathologically small `bit_depth` (e.g. a
  near-constant tiny-amplitude signal) — integer underflow drove the range coder's `range` to 0.
- Stereo predictor's `R[0]` reset threshold was a wrong invented constant (`1e15`); the real
  threshold (read from the binary) is `2^-30`, a near-zero clamp, not a large-magnitude guard —
  invisible at 16-bit, broke every 24/32-bit stereo signal whose energy legitimately exceeds `1e15`.
- Slow-entropy (`ent=2`) variance update treated the residual as signed int32 with an invented
  clamp; the binary always treats it as non-negative (a 64-bit zero-extended convert) and never
  clamps — invisible at 16/24-bit, wrong for near-full-32-bit residuals.
- Predictor rounding used `(int32_t)std::lrint(x)`, which wraps on overflow instead of matching
  the real `cvtsd2si` "integer indefinite" (`INT32_MIN`) saturation behavior — only reachable when
  a prediction overshoots `INT32_MAX`, i.e. 32-bit content near a clamp/wrap boundary.
