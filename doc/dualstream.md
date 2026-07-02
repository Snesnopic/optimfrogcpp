# OptimFROG DualStream (OFS)

Mirrored from [losslessaudio.org/DualStream.php](http://losslessaudio.org/DualStream.php).
Relevant to this project's own (not yet started) DualStream reimplementation — see
[status.md](status.md). Reference binaries: standalone `ofs`/`ofs.exe` tools, shipped alongside
`ofr` in the same official release packages (not part of `libOptimFROG.dylib`/`.so`'s exported
API — verified: `OptimFROG_open` fails on a `.ofs` file, so `ofs`'s decode logic must be
reverse-engineered from the `ofs` binary directly).

---

OptimFROG DualStream (OFS) is aimed at filling the big gap between perceptual coding and lossless
coding. The goal is to offer real transparent audio coding (not only perceptually transparent) at
half or less the bitrate generally used by lossless coding, and also to permit progressive
consistent increase of the quality level, until lossless coding is reached.

The OptimFROG DualStream branch was completely merged and it is fully maintained alongside the
OptimFROG Lossless project. The following text, included verbatim from a plain text file, is
currently outdated and corresponds to OptimFROG version 4.509. However, most of the information
contained about features and command-line options is still valid.

```
Features
========

This is a release version and it passed extensive successful testing
on around 37 GB of audio data. It uses algorithms mathematically
proven to be correct only. The base 4.5x file format is frozen.

It is a branch from the main OptimFROG version 4.509 project, with
modifications to allow the extended functionality and different
file extension (OFS - OptimFROG Small) in order not to mix lossless
and lossy files. Most probably, it will completely merge into the
main project in the next major release (in this release, only the
input plug-ins code was merged).


Advantages of OptimFROG DualStream compared with transform coders
(TC), such as MPC, OGG, MP3, AAC, WMA etc.:
- no clipping possible (the TC have this problem even at the highest
  bitrates with very loud music. When decoding, the synthesis filter
  may produce values greater than the maximum allowed integer value)
- no preecho possible (the TC always present some sort of preecho
  before transients. Preecho is a noise which appears before the
  actual transient. The strategy used in TC is to switch to shorter
  blocks when transients are detected to make preecho inaudible)
- no postecho possible (the TC make use of the postecho to save space
  and to mask overshots using the fact that up to 20 ms after a very
  loud sound, the hearing has diminished sensitivity)
- no overshot (overshot is using slightly greater quantization levels
  even after the signal partly changed its characteristics. This is
  mainly because of the block nature of the TC)
- listening level independent (the TC make use of the ATH to cut off
  frequencies under a certain level. However, this is dependent of
  the listening level, and does not always hold)
- suitable for further editing and transcoding (the TC make use of
  psycho acoustics, and produce distortions which generally make
  editing and further reencoding (even with the same coder) give
  inferior results)
- suitable for computer processing of audio data (from the point of
  the computer voice recognition, the TC make the audio data near
  unusable. For example, when encoding vocal formants, the TMN level
  in the respective critical band will add important noise,
  diminishing the clear tonal characteristics of the formants)
- full band (some TC use filtering to cut out very high frequencies)
- independent quantization levels for each channel (some TC use
  joined channel quantization, reducing spatial sound imaging)
- correction file option for lossless restoring of the original file
- error signal is very small in amplitude, flat, pure white noise
- no frequency domain artifacts
- excellent handling of artificial and low complexity audio data

Disadvantages of OptimFROG DualStream compared with transform coders
(TC), such as MPC, OGG, MP3, AAC, WMA etc.:
- as it does not take into account the human auditory system
  limitations, it needs a much higher bitrate (up to twice) to achieve
  perceptual transparency. However, together with reaching perceptual
  transparency, many other important audio qualities are preserved


I do not claim that OptimFROG DualStream is superior to the TC,
because they are targeted to do very different things.

TC are aimed to offer the highest compression rates while remaining
perceptually transparent. They are best suited at compressing finished
audio work, for listening and publication. The bitrate is generally
ranging from 96 up to 256 kbps.

Lossless compression, on the other hand is aimed to offer the smallest
file size while remaining true lossless. The great advantage of having
the original is paid by the file sizes, which are generally around
60% (850 kbps) of the initial size.

To eliminate the difficult problem of choosing between lossless and
near lossless, DualStream has an option to create a correction file,
which may be eventually stored separately and used at a later time to
restore the original. The advantage is that the two files, OFS and OFC
have together approximately the size of the lossless coded original
file (slightly greater with up to 1%).

The --quality 3 setting averages 339 kbps (~24%) and it should be
normally undistinguishable from the original file using ABX tests.

The --quality 5 setting averages 418 kbps (~30%) and it may be
considered truly transparent, suitable for archiving and
transcoding.

However, you may find that lower quality settings may be also
undistinguishable for you, also depending of the type of music.


WavPack (version 3.97) and OptimFROG DualStream
===============================================

I would like to thank to David Bryant, the WavPack author
for the idea of the correction file (a file which, when used
combined with the lossy file, permits lossless restoring of
the original file).
Although the two programs have common user functionality, they
are based upon very different principles. There is almost no
similarity between their implementation.
Also, thanks for his great program which made me consider
finishing and opening to the public OptimFROG DualStream, of
which first working version I did more than a year earlier,
in April 2002.


Advantages of OptimFROG DualStream compared to WavPack hybrid:
- distortions up to 15 times lower during sharp transients
- slightly lower distortions during quasi-stationary areas
- true separate quantization levels for each channel
- quality mode maintains constant quality thorough the whole file
- no overshots or postecho
- advanced noise shaping option, improving transparency
- up to 3% better lossless compression for OFS+OFC over WV+WVC
- fast seek, supported by the compressed file format
- error resilience, supported by the compressed file format
- exact bitrate control when using average bitrate

Disadvantages of OptimFROG DualStream compared to WavPack hybrid:
- decompression is 30% slower - 23.7x compared with 31.4x
  (all tests ran on my Athlon XP 1800+)
- compression is twice slower - 12.8x compared with 26.9x


There it is a short, speed illustrative example (test file used was
Yes - The Ladder - New Language, duration 9m19.1s):

No.     Bitrate   Enc.   Dec.  Command line used for encoding
1.   341.0 kbps  26.9x  31.4x  wavpack -h -b340
2.   345.9 kbps  12.8x  23.7x  ofs --mode fast --quality 3
3.   341.0 kbps   9.7x  16.7x  ofs --quality 3
4.   338.3 kbps   5.2x   8.0x  ofs --mode extra --quality 3
5.   244.0 kbps                ofs --mode fast --quality 0
6.   241.0 kbps                ofs --quality 0
7.   239.2 kbps                ofs --mode extra --quality 0

Note that for (2), (3), and (4) compressed files decode to the same
bit-identical file, so there is absolutely NO sacrifice in quality
by using either file. The only difference is that the fast mode file
has it this case 5 kbps greater bitrate than normal mode, and extra
mode file has 3 kbps smaller bitrate than normal mode.

When speed is of primary concern, you may use --mode fast along with
the desired quality

When you have a very high speed computer and speed does not matter
much, you may use --mode extra along with the desired quality, which
will save a few more kbps compared to normal mode, especially at very
high quality settings (quality >= 5). Using --mode extra with
quality <= 3.0 generally shows no significant kbps savings (<= 3 kbps)
and generally does not worth using (see positions (5), (6), and (7))


Quality (--quality) usage guidelines:
  0: ranging from 183 - 265, on average 236 kbps
     should be distinguishable from the original in ABX tests
  1: ranging from 209 - 295, on average 268 kbps
     possibly distinguishable from the original in ABX tests
  2: ranging from 242 - 326, on average 302 kbps
     possibly distinguishable from the original in ABX tests
  3: ranging from 277 - 362, on average 339 kbps, default
     normally undistinguishable from the original in ABX tests
  5: ranging from 351 - 443, on average 418 kbps
     transparent, suitable for archiving and transcoding
  6: ranging from 392 - 486, on average 458 kbps
     transparent, on average 3:1 compression for CD audio

Average bitrate (--bitrate) usage guidelines:
  -  quality mode generally produces better results at the same
     bitrate, especially at lower bitrates
  -  use average bitrate only when exact compression ratio is
     needed, such as when fitting files on a CD
  -  variable quality level is always >= 1, so it is meaningful
     to use average bitrate only for bitrates >= 300 kbps

You may enable advanced noise shaping at encoding (--ans), to get
improved transparency, especially at middle bitrates


Quick command line guidelines
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
For smallest file, on average 236 kbps

   ofs --quality 0 file.wav

For smallest file, but fastest decoding, on average 239 kbps

   ofs --mode fast --quality 0 file.wav

For high (default) quality compression, on average 339 kbps

   ofs --quality 3 file.wav

For very high quality, transparent compression, on average 418 kbps

   ofs --quality 5 file.wav

For transparent compression, on average 3:1 (458 kbps) for CD audio

   ofs --quality 6 file.wav
```

## Verified empirically (this project, 2026-07-02)

```
ofs --encode --correction --quality 3 file.wav --output x.ofs
```
produces `x.ofs` (lossy) + `x.ofc` (correction file, tiny — 17 bytes for a 3s test tone).
`ofs --decode --correction x.ofs` restores the original bit-exact. Confirms DualStream-with-
correction is a real, testable round-trip target for future RE work.
