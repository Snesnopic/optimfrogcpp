# ofrdecomp

A from-scratch C++ decompilation and reimplementation of the [OptimFROG](http://losslessaudio.org/)
lossless audio codec, reverse-engineered from the closed-source reference binary
(`libOptimFROG.0.dylib`, v5.100, macOS x86_64).

The goal is a decoder + encoder that reproduces the reference implementation **bit-exactly**,
verified directly against the original binaries rather than assumed from the format spec alone.

## Status

Both the decoder and the encoder are working and extensively verified:

- **Decoder**: bit-exact across mono/stereo, 8/16/24/32-bit, all predictor types (`pred=1` LPC,
  `pred=3` cascade NLMS), all entropy types (`ent=1` fast, `ent=2` slow, `ent=3` ACM), both
  post-processors (`post=1` identity/scale, `post=2` value-remap), and multi-block real-world audio.
- **Encoder**: produces valid, lossless `.ofr` files decodable by both our own decoder and the
  reference decoder, across the same pred/ent/post/bit-depth matrix, with a small preset-search
  mode (`OFR_BEST=1`) that picks the smallest output among a few safe configurations.

See [doc/status.md](doc/status.md) for the detailed coverage matrix and known residual gaps, and
[tests/conformance/](tests/conformance/) for the test suite that proves it — a bidirectional check
against the reference binaries (reference-encoded files decode identically to ours; our
encoder's files decode losslessly on the reference).

## Building

```sh
cmake -B build
cmake --build build
```

Produces three binaries in `build/`:

- `ofr` — unified CLI aiming for usage parity with the original tool:
  ```sh
  ofr --encode [options] src [--output dst] ...
  ofr --decode [options] src [--output dst] ...
  ofr --info src ...
  ```
  Supports `--preset {0-10,max}`, `--raw` + `--channelconfig`/`--sampletype`/`--rate` (headerless
  PCM), WAV in/out by default, `--output`, `--overwrite`, `--silent`, `--help`. Skips the original's
  rarely-used flags (`--selfextract`, `--deleteafter`, `--timestamp`, `--incorrectheader`, ...).
- `ofrdecomp` / `ofrenc` — the original minimal single-purpose CLIs (decode-only /
  positional-args encode), kept for backward compatibility with existing scripts.

## Repository layout

```
src/            decoder + encoder implementation
include/        public + internal headers
tests/          conformance test suite (bidirectional proof vs the reference binaries)
doc/            reverse-engineering notes and format documentation
```

## Testing

```sh
bash tests/conformance/run.sh
```

Runs the full bidirectional suite against a real-audio corpus plus a synthetic battery of
edge cases (silence, full-scale, degenerate/tonal signals, boundary lengths). See
[tests/conformance/run.sh](tests/conformance/run.sh) for environment knobs (corpus size, presets,
bit depths tested).

## Documentation

- [doc/status.md](doc/status.md) — current bit-exactness coverage and known gaps
- [doc/format.md](doc/format.md) — OFR container/bitstream format as reverse-engineered
- [doc/optimfrog_re_knowledge.md](doc/optimfrog_re_knowledge.md) — algorithm overview (range coder,
  entropy decoders, predictors, post-processors)
- [doc/pred3_analysis.md](doc/pred3_analysis.md), [doc/ent3_analysis.md](doc/ent3_analysis.md),
  [doc/post2_remap_analysis.md](doc/post2_remap_analysis.md) — deep dives on the cascade predictor,
  ACM entropy coder, and value-remap post-processor respectively

## License / provenance

This is a clean-room-adjacent reverse-engineering project built by dynamic and static analysis of
the reference binary for interoperability purposes. It is not affiliated with or endorsed by the
original OptimFROG author.
