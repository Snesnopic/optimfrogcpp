# OptimFROG IEEE Float (OFF)

Mirrored from [losslessaudio.org/IEEE_Float.php](http://losslessaudio.org/IEEE_Float.php).
Relevant to this project's own (not yet started) Float reimplementation — see
[status.md](status.md). Reference binary: standalone `off`/`off.exe`, shipped alongside `ofr` in
the same official release packages (not part of `libOptimFROG.dylib`/`.so`'s exported API, same
as DualStream — see [dualstream.md](dualstream.md)).

---

OptimFROG IEEE Float (OFF) provides advanced lossless compression for 32-bit floating-point audio
data. It supports all audio data types which use the 32-bit IEEE 754 floating-point format, with
full support for infinities, NaNs, negative zeros, and denormals. Optimal compression is achieved
for audio data with integer valued content, under each normalization convention.

The OptimFROG IEEE Float branch was completely merged and it is fully maintained alongside the
OptimFROG Lossless project. The following text, included verbatim from a plain text file, is
currently outdated and corresponds to OptimFROG version 4.509. However, most of the information
contained about features and command-line options is still valid.

```
Features
========

This is a release version, and it has been extensively tested. The
file compatibility with any future version is not implied or intended.

It is a branch from the main OptimFROG Lossless version 4.509 project,
with modifications and new algorithms to implement support for the IEEE
Float formats. The branch with support for IEEE Float started in
July 2002, and I recently decided to publicly release it (2003.10.04).

This version has no input plug-ins available for real-time playback
and the file format is not compatible and recognized by the mainstream
OptimFROG Lossless 4.509 version. Also, it does not support any integer
PCM wave formats. However, this version is completely compatible with
the previous 4.508e experimental version.

OptimFROG 4.509 [2004.04.18] has the following notable features:
  - first publicly available lossless audio compressor for IEEE Float
  - full support for 16.8 type 1 (Cool Edit), 24.0 type 1 (Cool Edit)
    and 0.24 type 3 (standard), normal and extended wave formats
  - full support for infinities, NaNs, negative zeros and denormals
  - optimal support for floating point data with integer-like content
  - on average 10-15% better lossless compression than any other
    multimedia-aware lossless compressor (like RAR3.x, ACE2.x, SZIP)
  - 2-10 times faster than other multimedia-aware lossless compressors
  - fast operation, default mode encodes 44100 Hz, 32 bits, stereo
    audio data at 9.0x real-time and decodes at 11.7x real-time on
    AMD Athlon XP 1800+
  - Win32 and Linux command line versions
  - extensible, streamable compressed format, tagging compatible
  - optimize option, further improving compression at no decoding cost
  - full raw file support
  - preprocess option to reduce effective mantissa bits before the
    actual compression


Usage
=====

The command line usage is identical to the mainstream OptimFROG
Lossless 4.509 version. See the ofr.txt file for the detailed command
line explanations. The only difference is that the executable has
been renamed to OFF (OptimFROG Float).

There is a supplementary option, named --mantissabits intended to
reduce the effective mantissa bits before the actual compression. The
23 mantissa bits can be reduced to 22 bits, up to 7 bits. This leads
to significant compression increase. The process is free from any
quantization distortion and any dynamic range reduction.
I suggest you may (always) use the --mantissabits 15 value, as it
gives around 25% compression improvement and it is indistinguishable
for any purposes from the original file.
There is also a big advantage of this process - decoding the file
and reencoding it (or any fragments of it) with the same or bigger
preprocessing option value does not introduce any additional changes
to the data.
As the preprocessing is done before the actual compression, you can
still use the --md5 option to ensure correct decoding (of the
preprocessed data).
```

## Verified empirically (this project, 2026-07-02)

```
off --encode --raw --channelconfig MONO --sampletype FLOAT32_1 --rate 44100 file.raw --output x.off
```
produces `x.off.ofr` (note the `.ofr` suffix is appended even though the tool is `off`).
`off --decode --raw x.off.ofr --output y.raw` restores the original bit-exact. The accepted
`--sampletype` values for `off` are `FLOAT32_1`, `FLOAT32_16`, `FLOAT32_24` (not `FLOAT32_0` as
listed in `format.txt`'s Appendix A sample-type table for the main `ofr` tool — `off` has its own
distinct set of accepted strings). `libOptimFROG.dylib`'s `OptimFROG_open` cannot open `.off`
files (`open fail`) — the decode logic lives only in the standalone `off` binary.
