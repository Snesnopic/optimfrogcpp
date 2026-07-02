# Documentation index

- [status.md](status.md) — current bit-exactness coverage matrix and known residual gaps.
  **Start here** for "what works" — it's the accurate, up-to-date reference.
- [format.md](format.md) — OFR container and bitstream format as reverse-engineered
  (main/HEAD/COMP/TAIL blocks, the range coder, algorithm dispatch).
- [optimfrog_re_knowledge.md](optimfrog_re_knowledge.md) — narrative algorithm overview (range
  coder, entropy decoders, predictors, post-processors). Its "current status" section is a
  historical snapshot from early in the project; see `status.md` instead for current state.
- [pred3_analysis.md](pred3_analysis.md) — deep dive: cascade NLMS predictor + LDLT final
  combiner (`pred_type=3`, presets 4-10).
- [ent3_analysis.md](ent3_analysis.md) — deep dive: ACM (advanced compression modeling) entropy
  coder (`entropy_type=3`, preset max).
- [post2_remap_analysis.md](post2_remap_analysis.md) — deep dive: value-remap post-processor
  (`post_type=2`, active for tonal/synthetic signals).

## Mirrored from losslessaudio.org

Reference material from the official project's own site, kept here so it survives independent
of the site staying up. Covers the format/algorithm/project itself, not the site or its author.

- [changelog.md](changelog.md) — full version history (OFR/OFS/OFF), verbatim.
- [features.md](features.md) — shipped feature list + the official "in progress" wishlist
  (useful for scoping what's worth reimplementing here vs. never-shipped).
- [dualstream.md](dualstream.md) — OptimFROG DualStream (OFS, lossy+correction) background,
  for the not-yet-started DualStream reimplementation.
- [ieee_float.md](ieee_float.md) — OptimFROG IEEE Float (OFF) background, for the not-yet-started
  Float reimplementation.
- [sdk_reference.md](sdk_reference.md) — SDK return codes and interface-layer notes not already
  captured verbatim in `include/OptimFROG.h`.
- [license.md](license.md) — official OptimFROG license terms and how this repo complies
  (no reference binaries committed; CI fetches them at run time, sha1-verified).
