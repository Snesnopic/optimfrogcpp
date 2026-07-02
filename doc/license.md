# OptimFROG license terms

Mirrored from [losslessaudio.org/License.php](http://losslessaudio.org/License.php) for
reference when redistributing any official OptimFROG binary (reference dylib/so, `ofr`/`ofs`/`off`
CLI tools) as part of this project's testing infrastructure. This project's own source code is
not covered by these terms — they apply only to the original, closed-source OptimFROG binaries.

---

OptimFROG is completely free for personal, and also for non-commercial and commercial use.
However, in the case of redistribution of the Release Packages, binary Libraries, SFX Decoder
archives, and binary command-line Encoders the following terms and conditions apply.

## Release Packages

You are hereby permitted to redistribute the unmodified Release Packages under their original
name, through, but not limited to, FTP, WEB sites, storage media associated with printed
magazines, or physical devices. Redistributing any modified, where modification means any
change to the contents of a file, or renamed Release Package is not permitted. An exception is
provided for the redistribution of the Release Packages as part of an operating system official
repositories (such as, but not limited to, Debian, Ubuntu, FreeBSD, or Fedora), where a relevant
subset of the unmodified files from the original Release Package may be partially renamed* and
repackaged as considered technically necessary. Additionally, charging a fee or requesting
donations for downloading or otherwise providing any Release Package separately is not
permitted. **Please do not directly link to the Release Packages on the OptimFROG website, but
instead link to the main OptimFROG download page.**

\* An exception is granted for partial renaming of individual binary files, but not Release
Packages, such that a prefix or suffix can be added to the original binary file name if
considered necessary for versioning, platform compatibility, or other technical reasons (for
example, renaming to `libOptimFROG.so.1`, `ofr64`, or `OptimFROG64.dll`).

## Binary Libraries

You are hereby permitted to redistribute the unmodified binary Libraries under their original
name (such as, but not limited to, `OptimFROG.dll`, `libOptimFROG.so`, and `libOptimFROG.dylib`)
along with other software or physical device which uses such Libraries by means of the provided
SDK. Redistributing any modified, meaning any change in the contents of a file including
executable re-compression, or renamed binary Library is not permitted, with an exception for
some partial renames*.

## Binary Command-Line Encoders

You are hereby permitted to redistribute the unmodified binary command-line Encoders under their
original name (such as, but not limited to `ofr`, `ofr.exe`, `ofs`, `ofs.exe`, `off`, and
`off.exe`) along with other free or open source software which uses such Encoders. **Sending a
notification email to the OptimFROG author is required**, which must include your name, the name
of your free or open source software, and the Internet address where your software can be
downloaded. However, for the cases of a physical device, and commercial or ad-supported software,
such redistribution of the unmodified binary command-line Encoders under their original name
requires prior written permission from the OptimFROG author, by sending of an email request. The
email request must include your name, the name of your software or physical device, and the
Internet address where your software can be downloaded, or where the physical device is described
or advertised. Redistributing any modified, meaning any change in the contents of a file
including executable re-compression, or renamed binary command-line Encoder is not permitted,
with an exception for some partial renames*.

## SFX Decoder Archives

Any OptimFROG self-extracting compressed archive created using the binary command-line Encoders
by incorporating an unmodified binary SFX Decoder (such as, but not limited to, `ofr_sfx.exe` or
`ofr_sfx`) can be redistributed under any name and without limitations, subject only to the
conditions associated with the uncompressed source file.

## Warranty disclaimer

OptimFROG and all accompanying software is distributed "AS IS". No warranty of any kind is
expressed or implied. Use it at your own risk. The author will not be liable for data loss,
damages, loss of profits or any other kind of loss while using or misusing this software. Use of
OptimFROG or any accompanying software indicates you agreed to all of the above conditions.

---

## How this project complies

- `libOptimFROG.0.dylib`/`libOptimFROG.dylib` (Binary Library) and the `ofr` command-line
  Encoder are **not committed to this repository**. CI fetches the official release packages
  directly from losslessaudio.org at run time (SHA1-verified against the published checksum,
  never stored or redistributed by us) — see `.github/workflows/conformance.yml`.
- Only the OFR container **format itself** (reverse-engineered from the binaries, documented in
  [format.md](format.md)) and small `.ofr` test fixtures we generated (`tests/*.ofr`, plain
  encoded audio, not covered by the Release/Library/Encoder categories above) are checked into
  git.
