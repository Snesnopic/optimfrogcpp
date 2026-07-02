# OptimFROG changelog

Mirrored verbatim from `changelog.txt` (bundled with the official release packages) and
[losslessaudio.org/Changes.php](http://losslessaudio.org/Changes.php), covering OFR (Lossless),
OFS (DualStream), and OFF (IEEE Float) — the three formats/tools produced from the same OptimFROG
codebase. This project currently reimplements OFR only (v5.100); OFS/OFF are tracked separately
(see [dualstream.md](dualstream.md), [ieee_float.md](ieee_float.md)).

```
OptimFROG Lossless + DualStream + IEEE Float Audio Codec v5.xxx
Copyright (C) 1996-2016 Florin Ghido, all rights reserved.
Visit http://LosslessAudio.org/ for updates and more information.
@OptimFROG is also on Twitter. E-mail: florin.ghido@gmail.com




OFR + OFS + OFF version 5.100 and SDK version 1.600 [2016-09-02]:
  - add --preset 0 to --preset 10 command-line options for the most
    efficient compression presets, backward compatible with v4.600
  - add more efficient versions of the highnew, extranew, bestnew, and
    ultranew modes, named highnew-light, extranew-light, bestnew-light,
    and ultranew-light, backward compatible with v4.520
  - add turbonew, fastnew, and normalnew efficient compression modes to
    complement the extra mode, backward compatible with v4.520
  - add the --aca and --acm aliases and the negated form --no-aca
  - add the minDecoderVersion optional field to the main block, which
    has the minimum decoder version required to decode the file
  - a value of 1000 and 2000 can be added to any preset level in order
    to enable --acm and --optimize best, respectively
  - add --preset max to replace --maximumcompression --experimental
  - show for each preset its complexity and compression gain, relative
    to preset 0, and equivalent options in the command-line help
  - speed increase of around 10% for all *new modes, such as highnew,
    extranew, and bestnew, by improved SSE optimizations
  - speed increase of around 10% for all non *new modes, such as fast,
    normal, and high, by newly added SSE2 optimizations
  - generate txz files instead of tar.gz for FreeBSD release packages
  - small documentation additions, updates, and information about the
    newly added modes and the minDecoderVersion optional field
  - many internal small code improvements and updates


OFR + OFS + OFF version 5.003 and SDK version 1.500 [2015-11-06]:
  - new official release packages with binaries and SDK are available
    also for FreeBSD/x86, FreeBSD/x86+SSE2, and FreeBSD/x64. Thanks to
    Matthew Rezny for suggestion, information, and testing
  - add --writefreshheader option to write a fresh standard WAV header
    during decoding, replacing any stored header and footer
  - add standard interactive installer file install.sh, which copies
    the binary command-line encoders, the binary SFX decoder, the
    binary library, the SDK headers, and the pkg-config configuration
    file optimfrog.pc to their usual file system locations, on Linux,
    OS X, and FreeBSD. Thanks to Chris Spiegel for contributing the
    initial version of the install.sh and optimfrog.pc.in files
  - add versioning support to the shared object binary library, using
    the standard naming conventions, on Linux, OS X, and FreeBSD.
    Thanks to Chris Spiegel for suggestion, information, and testing
  - add OptimFROG to the relative path for all includes in the SDK
    examples. After installation with install.sh, the SDK examples
    can also be built using the included pkg-config definitions for
    the OFR_INCLUDE and OFR_LIBRARY variables from the Makefile
  - decoder speed improvements for --advanced-compression-analysis and
    --advanced-compression options, if there was no compression gain
  - add to the command-line help the --advanced-compression-analysis
    and --advanced-compression-modeling undocumented options
  - allow the --experimental, --advanced-compression, and the other
    two related options only for the Lossless variant, because they
    were not intended to work or be compatible with DualStream
  - many internal code improvements and updates, which provide small
    speed improvements for all compression modes under most platforms


OFR + OFS + OFF version 5.002 and SDK version 1.401 [2015-08-18]:
  - significant compatible compression improvements, around 4%, for the
    fast, normal, and high modes when using --optimize none, for stereo
    files with mono or near mono content
  - fixed an issue with --advanced-compression which triggered an
    out-of-memory exception during encoding of data blocks containing
    only 1 sample point. Thanks to Skymmer for reporting
  - fixed a file handle leak in the SDK when trying to open files which
    are not OptimFROG files. Thanks to Chris for reporting
  - fixed a rare issue where a non-conforming MD5 hash was computed for
    files with an audio data size from 56 to 63 modulo 64. These hashes
    can still be checked normally using the command-line encoders. No
    file from Audio CDs is affected. Thanks to Robert for reporting


OFR + OFS + OFF version 5.001 [2015-07-16]:
  - fixed WAVE parser with --incorrectheader option to accept all WAV
    files with incorrect RIFF and data chunk sizes in the WAV header,
    and correct these sizes in the stored WAV header after encoding


OFR + OFS + OFF version 5.000 and SDK version 1.400 [2015-06-19]:
  - a new unified, simplified, and more permissive license which allows
    both non-commercial and commercial use, and also redistribution
  - new official release packages with binaries and SDK are available
    also for Win/x64, OSX/x86+SSE2, and OSX/x64
  - new official release packages with binaries and SDK built with SSE2
    enabled are available for Linux/x86+SSE2 and Win/x86+SSE2
  - the SDK is now included with the release package for each platform
  - many internal code improvements and updates, which provide various
    speed improvements for all compression modes under each platform
  - fully reviewed, significantly reduced, and reorganized all platform
    dependent source code and predefined macros
  - added portable generic versions for all platform dependent source
    code, to become fully compliant with C++03 and C99 standards
  - source code also compiles cleanly with Clang under each platform
  - updated SDK examples, completely changed SDK directory layout, and
    separated all the plug-ins source code from the SDK
  - added --advanced-compression command-line option to the encoders in
    order to replace the --experimental deprecated option name
  - added support for files larger than 2 GB also to the Linux and OSX
    32-bit command-line encoders using POSIX Large File Support (LFS)
  - merged the implementation in OptimFROGDecoder.cpp for the thin C++
    wrapper around the C-style interface into OptimFROGDecoder.h
  - all compressed sizes are identical to those from the 4.910b version
  - updated and merged SDK and format documentation, and the changelog
  - new scripts to create and verify release packages for each platform


OFR + OFS + OFF version 4.910b and SDK version 1.300 [2011-02-10]:
  - fully backward compatible with previous versions
  - slightly better compression for highnew, extranew, bestnew
    modes, and also for --maximumcompression
  - parsing WAV files with invalid data chunk size in WAV header
    using --incorrectheader (for foobar2000 and other command
    line format converters using pipes who cannot compute the WAV
    file length in advance and generate a 4 GB header instead)
  - many internal source code improvements

  - [other] updated foobar2000 input plug-in for foobar2000 1.1.x
  - [other] updated OptimFROG SDK to version 1.300 (included here)


OFR version 4.800a [2008.05.05]:
  - includes only Win32 binary
  - internal source code improvements
  - not publicly released on the website


SDK version 1.220b [2008.02.25]:
  - the OptimFROG library is now also available for Darwin as a
    DYLIB universal (ppc and x86) library
  - added a FMOD SoundSystem Win32 plug-in named codec_ofr
  - not publicly released on the website


OFR version 4.600ex [2008.02.25]:
  - includes only Darwin universal (ppc and x86) binary
  - successfully ported to Darwin/x86
  - not publicly released on the website


OFR version 4.600ex [2006.06.26]:
  - the highnew, extranew, bestnew modes and maximumcompression get
    slightly better compression (on average 0.1%)
  - added --experimental option, enabling advanced experimental
    compression, which is NOT backward compatible with any previous
    4.5xx versions
    * use with any mode for greatly improved compression for some
    files (on average, 0.60% better compression for Audio CDs)
    * extranew speed: encoding < 35% slower, decoding < 11% slower
    * can be also used together with --maximumcompression

  - [other] the foobar2000 input plug-in now supports cue sheets
    (thanks to Artur for suggestions and testing)
  - [other] official GUI interface using FroG is ready, it will be
    included in the upcoming OptimFROG installer (thanks to Daniel,
    the FroG author; available at http://frog.objective-view.de/)
  - [other] parsing WAV files with invalid headers is in the works


OFR + OFS + OFF version 4.520b1 [2006.04.18]:
  - the port to Darwin/ppc was verified and is now released (thanks
    to krmathis for testing)
  - added --tailsize option to specify the tail bytes for raw files
  - fixed MD5 internal computing for big endian architectures
  - added run-time self integrity architecture check
  - fixed --check_audition option which used a condition too strict
    and did not correct all the corrupted zeros


OFR + OFS + OFF version 4.520b and SDK version 1.210b [2006.03.02]:
  - successfully ported to Linux/amd64
  - successfully ported to Darwin/ppc (PowerPC G3)
  - many internal source code improvements
  - all the newly created compressed files are
    completely backwards compatible (can be
    decoded with previous 4.50x versions)
  - added ID3v2 tag support (all decoders can
    search for main header, skipping up to 1MB)

  - added --selfextract option, the Win32/x86 sfx
    stub (statically linked) is only 55 kB in size
  - complete self extracting support for Win32/x86,
    Linux/x86, and Linux/amd64 (all sfx stubs are
    statically linked)
  - C source code for sfx stub available in SDK

  - slightly faster encoding and decoding with
    exactly the same compression
  - compression improved very slightly when using
    --optimize best option
  - improved compression (0.1-0.3%) when using
    command --maximumcompression (this option
    is mainly intended for benchmark)

  - added --correct_audition option to IEEE Float
    to correct Adobe Audition / CoolEdit conversion
    bug: when converting from int to float, Audition
    converts 0 values to random noise with maximum
    relative amplitude < 5*10^-16; this option
    carefully corrects this bug setting them back
    to 0 and significantly improves compression
    ratio in these files

  - the OptimFROG.dll/.so version 1.210 is binary
    compatible with the previous versions, now also
    available for Linux/amd64 and Darwin/ppc
  - fixed a missing check for errors after calling
    OptimFROG_readTail(...), which could return -1,
    in Test_C and Test_CPP SDK samples


SDK version pr1.200_2 [2005.09.21]:
  - fixed a typing bug in the new C++ based interface, which caused
    wrong display of tags. Winamp5, dBpowerAMP, XMMS plug-ins, and CPP
    examples were recompiled (unfortunately, the Linux binaries were
    now compiled on Fedora Core 2, but you can recompile them on your
    system using the included scripts). Thanks to Andrea for reporting


OFR + OFS + OFF version pr4.510_2 [2005.09.21]:
  - small fixes and improvements


SDK version pr1.200_1 [2005.07.24]:
  - fixed a problem with deallocation of tags in some situations,
    causing a crash when OptimFROG_freeTags is called


SDK version pr1.200 [2005.07.17]:
  - OptimFROG.h now uses PortableTypes.h and SystemID.h in order to
    have portable types and infrastructure
  - the library is now available also for Linux as OptimFROG.so
  - added OptimFROG_freeTags to release the memory allocated for the
    key and value pairs in the tags structure
  - added a C++ based interface OptimFROGDecoder.h, having greater
    flexibility and requiring less work for writing plug-ins
  - all type names were appended _t to avoid name clashes
  - backward compatible type names are defined as macros
  - there are two new examples for using the C++ based interface
  - small fixes and improvements
  - based on the OptimFROG internal engine version 4.510


OFR + OFS + OFF version pr4.510 [2005.07.17]:
  - encoding is now 6% faster with exactly the same
    compression
  - all the source code was reorganized and rewritten
    to use uniform standards and increase portability
  - OFR, OFS, OFF and the DLL/SO library were merged
    into a single, common code base
  - significantly reduced system dependent code areas
  - all programs are now Valgrind clean
  - fixed small problems (like not setting the file
    date for the correction file)

  - the OptimFROG library is now available for Linux
    as a SO library
  - the OptimFROG.dll version 1.200 is binary
    compatible with the previous versions
  - added new function OptimFROG_freeTags to release
    the memory allocated for the tags structure
  - new C++ wrapper interface for the OptimFROG
    library requiring less work for writing plug-ins
  - two C usage examples and two C++ usage examples
    of the library for Windows/Linux

  - complete source code for dBpowerAMP, Winamp 5,
    foobar2000, and XMMS plug-ins

  - site address has changed to (the other are still
    usable) http://www.LosslessAudio.org


SDK version 1.100 [2004.04.21]:
  - not binary compatible with the 1.000e experimental version, the
    applications must be recompiled (without any source code changes)
  - fixed a problem where the file handle was not released when getting
    file information using the infoFile function. Thanks to Florian for
    reporting
  - fixed a problem where no error code was returned when the function
    decodeFile failed to write data, when disk was full
  - added two text fields, sampleType and channelConfig to the Info
    structure
  - updated the sample command line programs to have complete error
    checking, reporting and return codes
  - small updates to the foobar2000 plug-in. Thanks to Case for
    reporting and help
  - small fixes and improvements
  - based on the OptimFROG internal engine version 4.509


OFF version 4.509 [2004.04.18]:
  - added complete raw file support
  - added verify command
  - added --mantissabits option to reduce effective
    mantissa bits before the actual compression


OFR + OFS version 4.509 [2004.04.13]:
  - [OFS] around 10% faster encoding in average bitrate mode at bit
    identical results
  - enhanced the WAV file parser to automatically detect uneven size
    chunks without the required alignment byte (non-conforming files)
  - slight general speed enhancement with around 2%
  - added the --headersize option to specify the starting header size
    for raw files, also useful for raw data alignment
  - enhanced the raw file parser to add the last few bytes to the tail
    instead of rejecting the file for having a non-divisible data size
  - enhanced automatic generation of file extension at decoding (using
    the header data to detect if the file is WAV or raw)
  - added full support for the data types SINT8, UINT16, UINT24, UINT32
  - fixed a very rare encoder bug where an invalid compressed block was
    created for some silence-like data. Thanks to Olivier for reporting


SDK version 1.000e [2004.02.22]:
  - first public release, experimental version


OFF version 4.508e [2003.10.04]:
  - first IEEE Float public release, experimental version


OFR + OFS version 4.507 [2003.09.28]:
  - fixed wildcards support, where files not in the current directory
    were not expanded correctly with their path
  - fixed some small memory allocation problems
  - small fixes and improvements


OFS version 4.507b1 [2003.07.24]:
  - corrected a very rare problem with 24 bit mono files when using
    --ans which could make the encoder crash (only ofs.exe updated)


OFS version 4.507b [2003.07.20]:
  - stable format, not anymore alpha stage
  - new correction file format, works now with the --ans option
  - corrected the --ans (advanced noise shaping) option, which
    previously used suboptimal formulas, and works better now
  - no longer creates pure lossless files, use OFR instead
  - default (no command line switches) encodes at --quality 3
  - corrected some small decoder problems with high amplitude signals
  - the --check option works with --correction to check MD5 hash
  - new input plug-ins (compiled also for dBpowerAMP and Winamp3)
  - decoding compatible with the alpha versions (4.507a and 4.507a2),
    but not with their correction files
  - removed raw file support, which was not implemented
  - small fixes and improvements


OFS version 4.507a2 [2003.06.25]:
  - added advanced noise shaping at encoding (--ans), improving
    transparency, especially at middle bitrates


OFS version 4.507a1 [2003.06.23]:
  - 210% faster encoding in quality mode at bit identical results
  - 220%-270% faster encoding in average bitrate mode at bit identical
    results
  - not publicly released


OFS version 4.507a [2003.06.21]:
  - first DualStream version


OFR version 4.506 [2003.05.31]:
  - added wildcards support for matching multiple source files
  - added the new highnew compression mode, 25% faster than extranew
    mode with very similar compression
  - the encoder now writes the exact compression mode used in the
    informational purpose field of the header (instead of just extra
    for the extranew mode)
  - added the --deleteafter option to delete the source file after
    successful encoding or decoding
  - added the --timestamp option to copy the source file's time stamp
    to the destination file when encoding or decoding
  - added the --md5 option to store MD5 hash of original raw PCM input
    data when encoding, and the --check command to check it later
  - made the --verify command work also silent
  - the --speedup option is now deprecated
  - fixed an important memory leak in the decoder common plug-in
    interface
  - fixed a rare decoder bug where files encoded with bestnew mode and
    speedup 1x would fail to decode. Thanks to Eltoder for reporting
  - small fixes and improvements


OFR version 4.505 [2003.05.14]:
  - enabled compression to a pipe and decompression from a pipe
  - fixed a problem with rejection of some non-seekable raw input files
  - fixed a rare encoder problem with mono files and extranew mode
  - the ultra and insane compression modes are now deprecated
  - fixed crashing under Windows 95 due to SSE detection problem.
    Thanks to Case for reporting and help
  - small fixes and improvements


OFR version 4.504b [2003.04.16]:
  - created this file
```
