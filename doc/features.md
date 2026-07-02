# OptimFROG features

Mirrored from [losslessaudio.org/Features.php](http://losslessaudio.org/Features.php) and
[losslessaudio.org/InProgress.php](http://losslessaudio.org/InProgress.php), for reference when
scoping this project's own feature coverage against the original. See
[status.md](status.md) for what this reimplementation actually covers today.

## Shipped features (official OptimFROG, all variants)

- usually the best lossless audio compression ratios
- optimal support for all integer PCM wave formats up to 32 bits
- fully featured Windows Media Player, foobar2000, Winamp 2/3/5, dBpowerAMP, XMPlay, QCD, and
  XMMS input plug-ins for playback
- fast operation, the default mode (preset 2) encodes CD quality audio data at 66.9x real-time
  and decodes at 96.9x real-time on Intel Core i7-6700HQ at 2.6 GHz, while the fastest mode
  (preset 0) encodes at 140.0x real-time and decodes at 138.0x real-time
- Windows, Linux, OS X, and FreeBSD command line versions
- simple to use, but powerful Windows GUI front-end, Kermit (made by Speek)
- extensible, streamable compressed format, tagging compatible
- optimize option, further improving compression at no decoding cost
- backward compatible with version 4.2x (decode only)
- 64 bits large file support under 32-bit versions of Windows, Linux, OS X, and FreeBSD
- complete pipe support for encoding and decoding
- complete raw file support
- quick verify compressed file integrity function
- compatible with Exact Audio Copy, with ID3v1.1 tagging
- extensible command line format
- multiple file processing on the same command line, with wildcards
- option to store MD5 of raw PCM input data and function to check it
- option to delete source file after successful operation
- option to copy source file time stamp to destination file
- bitstream error resilience and transparent real-time recovery
- fast seek with intelligent caching for plug-ins
- ID3v1.1 and APEv2 read tagging support for plug-ins, ID3v2 compatible
- streaming support (playing HTTP streams) for foobar2000 plug-in
- Replay Gain compatible plug-ins for foobar2000 and Winamp3
- complete support for creating self-extracting (sfx) archives
- fully featured SDK for using compressed files in any application

## "In progress" (the official project's own wishlist — never shipped as of v5.100)

This is the list of new features which were planned for future OptimFROG versions, per the
website. **None of these are present in v5.100** (the version this project targets), so there is
no reference binary behavior to reverse-engineer or match for any of them — pursuing them here
would be from-scratch design, not RE.

- Adobe Audition (formerly CoolEdit) fully functional plug-in
- multichannel support
- recovery information at creation and repair for corrupted files
- uLaw and ALaw data types support
- automatic speed calculation for all encoding and decoding modes
- gapless joining of multiple OptimFROG files
- integrated GUI interface and automated installer with plug-ins
- function for specifying the DualStream `.ofc` correction file in the SDK
- "something completely different and new, OptimFROG Asymmetric"

## Notes for this project's own scope

- Plugin/GUI features (Winamp, foobar2000, WMP, dBpowerAMP, XMPlay, QCD, XMMS, Kermit) are
  out of scope — each is a separate technology stack unrelated to the core codec.
- `--headersize`/`--tailsize`, `--md5`/`--check`, `--verify`, and pipe support (via the SDK
  `ReadInterface`) are implemented in the `ofr` CLI. RECV-block recovery and decoder seek are
  not yet implemented — see [status.md](status.md).
- uLaw/ALaw and multichannel are explicitly excluded here too, for the same reason they're
  unimplemented upstream: no shipped reference behavior exists to verify against.
