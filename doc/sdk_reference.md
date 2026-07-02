# OptimFROG SDK interface reference

Condensed from [losslessaudio.org/SDK.php](http://losslessaudio.org/SDK.php). The full data
structure and function declarations are already mirrored verbatim in this repo's
[include/OptimFROG.h](../include/OptimFROG.h) and
[include/OptimFROGDecoder.h](../include/OptimFROGDecoder.h) (the SDK headers themselves); this
file only captures the parts that live in the SDK's prose documentation and aren't otherwise
in the codebase — mainly the return codes.

## Return result codes

Used by `OptimFROG_decodeFile` and `OptimFROG_infoFile` (and, in this reimplementation, wherever
the same `OptimFROG_NoError`-style codes are checked):

| Code | Value | Meaning |
|---|---|---|
| `OptimFROG_NoError` | 0 | No errors detected. |
| `OptimFROG_MemoryError` | 1 | The process failed because of insufficient memory. |
| `OptimFROG_OpenError` | 2 | The process failed because the source file cannot be opened. |
| `OptimFROG_WriteError` | 3 | The process failed because the destination file cannot be opened, or a write failed on the destination file. |
| `OptimFROG_FatalError` | 4 | The process failed because of a fatal error, like a truncated source file. |
| `OptimFROG_RecoverableError` | 5 | One or several recoverable errors appeared (i.e. broken data blocks, which are replaced with silence). |

## SDK layers

The official SDK ships two interface layers over the same binary Library, both already present
in this repo's headers:

- **C-style interface** (`OptimFROG.h`) — usable from both C and C++, `extern "C"`,
  manual `createInstance`/`destroyInstance` lifecycle. This is what this project's `OFR_*`
  core classes are exposed through today (see [status.md](status.md)).
- **Thin C++ class wrapper** (`OptimFROGDecoder.h`) — a small RAII-ish wrapper around the
  C-style interface, not a from-scratch modern API. Distinct from the fuller modern-C++ facade
  discussed as a possible future addition to this project (namespaced, `std::span`-based,
  exceptions) — see the project task list.

## Custom stream reading (`ReadInterface`)

`OptimFROG_openExt` accepts a `ReadInterface*` — a struct of function pointers
(`close`/`read`/`eof`/`seekable`/`getPos`/`seek`) — letting a caller feed the decoder from a
non-seekable source (e.g. a network stream or pipe) instead of a plain file. This is what the
official Features page calls "complete pipe support" — it's an SDK-level capability, not a CLI
flag; the official `ofr` CLI itself doesn't accept `-` for stdin/stdout either (verified
empirically against v5.100). This project's `OptimFROG_openExt` already implements the
non-seekable path.
