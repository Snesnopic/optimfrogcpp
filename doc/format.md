# OFR container & bitstream format

As reverse-engineered from `libOptimFROG.0.dylib` v5.100. The official SDK ships a
`SDK/Documentation/format.txt` alongside the reference binaries — treat this document as the
implementation-verified companion to that spec (the decoder's behavior is the source of truth;
where the two disagree, what the binary actually does wins).

## Container layout

```
OFR  main_block   (fixed layout, see below)
HEAD head_block    (opaque passthrough, e.g. original WAV header bytes)
COMP* comp_block   (one or more compressed audio chunks)
TAIL tail_block    (opaque passthrough, e.g. trailing WAV bytes)
```

### Main block (`OFR `)

| Field | Size | Notes |
|---|---|---|
| magic | 4 | `"OFR "` (`0x2052464f` little-endian) |
| size | 4 | main block size (`0x11` in current files; `0x0f` in older ones, no extra version field) |
| total_samples_low | 4 | low 32 bits of total sample count (frames, not values) |
| total_samples_high | 2 | high 16 bits |
| sample_type | 1 | `0/1`=8-bit, `2/3`=16-bit, `4/5`=24-bit, `6/7`=32-bit (even/odd = unsigned/signed) |
| channel_config | 1 | `0`=mono, `1`=stereo (else falls back to stereo) |
| samplerate | 4 | Hz |
| version+pad | 2 | `version = (val>>4) + 0x1194`; low 4 bits = pad |
| speedup+method | 1 | `speedup = val&7`, `method = val>>3` |
| extra_version | 2 | only present if `size >= 0x11`; skipped, not used by decode |

### HEAD / TAIL blocks

`"HEAD"` / `"TAIL"` magic + `uint32_t size` + `size` opaque bytes (typically the original
container's header/trailer bytes, e.g. a WAV RIFF header, passed through verbatim).

### COMP block (one per encoded chunk)

| Field | Size | Notes |
|---|---|---|
| magic | 4 | `"COMP"` (`0x504d4f43`) |
| compressed_size (`D`) | 4 | total bytes in this COMP block from the CRC field onward |
| crc32 | 4 | zlib CRC32 over the range-coded payload (the `D-4` bytes following this field... see below) |
| uncompressed_size | 4 | decoded **values** in this block (frames × channels) |
| sample_type_byte | 1 | mirrors the main block's sample_type for this chunk |
| channel_config_byte | 1 | mirrors channel_config |
| reserved | 2 | `pred_type = (reserved>>6)&0x1f`, `entropy_type = reserved>>11`, `post_type = reserved&0x3f` |
| encoder_id byte | 1 | skipped by the decoder; the range stream's dummy first byte doubles as a second ID byte |
| range-coded payload | `D-13` | the actual compressed audio, decoded via `OFR_RangeCoder` |

Payload length is bounded (`compressed_size - 13`) so the range coder never over-reads into the
next COMP block; after decoding the block's `uncompressed_size` values, the reader realigns to
the payload boundary before reading the next `COMP` magic.

### Dispatch (`reserved` word → algorithm selection)

- `pred_type`: `0`=none, `1`=LPC (`OFR_Predictor`/`OFR_PredictorStereo`), `3`=cascade NLMS
  (`OFR_PredictorCascadeMono`/`Stereo`), `4`=DualStream/lossy (not implemented here — the
  reference decoder supports it, we don't).
- `entropy_type`: `0`=none, `1`=fast (variance-exponent-indexed contexts), `2`=slow
  (master-tree + bit-width escape), `3`=ACM (integer context modeling).
- `post_type`: `0`=none, `1`=identity/scale (`mult`+`offset` per channel), `2`=value-remap
  (dense-index → sparse original value, for tonal/synthetic signals).

`data_bits` — the bit-width driving the predictor's shift-wrap trick and the entropy coder's
depth — is derived from the *post-processed* value range, not the container's raw
`sample_type` bit depth: `data_bits = ofr_bitlen(min, max)` over the (channel-combined, for
stereo) coded range. For `post=1` with a non-identity `mult`, this coded range is
`(min-offset)/mult .. (max-offset)/mult`, not the stored original min/max — see
`doc/status.md`'s "known residual gaps" for the one case (`mult == ±2^31`) where this reduction
still has an unresolved edge.

## Range coder

31-bit-window carryless-ish range coder (`OFR_RangeCoder`):

- Init: read a dummy byte (ignored), read a second byte into `cache`, `range=0x80`,
  `value=cache>>1`, then `normalize()`.
- `normalize()`: `while (range < 0x800001) { value = (cache&1)<<7 | value<<8; cache = next_byte();
  value |= cache>>1; range <<= 8; }`
- Uniform bits, tree-coded symbols (segment-tree frequency model with periodic halving), and a
  golomb-style helper are all built on top of this core loop — see
  `include/optimfrog_decoder.h`'s `OFR_RangeCoder` for the exact bit-for-bit transcription.

## Predictors, entropy coders, post-processors

See the dedicated deep-dive docs:

- [optimfrog_re_knowledge.md](optimfrog_re_knowledge.md) — algorithm overview across all
  components.
- [pred3_analysis.md](pred3_analysis.md) — cascade NLMS + LDLT final combiner (`pred=3`).
- [ent3_analysis.md](ent3_analysis.md) — ACM context-modeling entropy coder (`ent=3`).
- [post2_remap_analysis.md](post2_remap_analysis.md) — value-remap post-processor (`post=2`).
