# OptimFROG Reverse Engineering Knowledge Base

This document serves as the master knowledge base for the reverse engineering of the OptimFROG audio codec. It tracks the format details, algorithmic discoveries, and the current state of decompilation as of June 2026.

## 1. Bitstream and Block Structure

An OptimFROG `.ofr` file is composed of a main file header (similar to a standard WAV/RIFF header extended with OFR metadata) followed by a series of compressed audio chunks.

### Chunk Header
Every audio chunk begins with the following header layout:
* `uint32_t magic`: `0x504d4f43` ("COMP")
* `uint32_t compressed_size`: Number of compressed bytes in this chunk
* `uint32_t skipped`: Unused/Skipped bytes
* `uint32_t uncompressed_size`: Expected decompressed size
* `uint8_t uVar8`: Flag (usage TBD)
* `uint8_t bVar2`: Flag (usage TBD)
* `uint16_t uVar15`: **Configuration Flags** (Crucial for decoding)
* `uint8_t crc`: Checksum/CRC-8 of the chunk

### Configuration Flags (`uVar15`)
The 16-bit `uVar15` flag dictates which algorithmic blocks are instantiated for decoding:
* `entropy_type`: `(uVar15 >> 11)` - Type of entropy decoder to use (e.g., 0 = None, 2 = Type 2)
* `pred_type`: `((uVar15 >> 6) & 0x1f)` - Type of decorrelator/predictor. `0` usually means no predictor, but in some configurations it selects the Base Predictor, while `1` selects `PredictorStereo`, etc.
* `post_type`: `(uVar15 & 0x3f)` - Type of post-processor.

---

## 2. Core Algorithmic Components

OptimFROG uses a highly asymmetric encoding/decoding approach. Decoding involves three main pipelines: **Entropy Decoder** -> **Predictor (Decorrelator)** -> **PostProcessor**.

### 2.1 Range Coder (`OFR_RangeCoder`)
The underlying bitwise stream reader uses a 32-bit Range Coder.
* **State variables:** `range` (initialized to `0xffffffff`) and `value` (read from first 4 bytes of stream).
* **Reading Uniform Bits:** The coder computes `step = range >> bits`. The returned value is `value / step`. It then updates `value -= val * step` and `range = step`.
* **Reading Frequencies (Tree):** `step = range / total_freq`. Value is `value / step`. It searches the tree for the symbol whose cumulative frequency encapsulates the value.

### 2.2 Entropy Decoder (`OFR_EntropyDecoder`)
We have fully reverse-engineered **Type 2** entropy.
* **Initialization:** First decodes an "alphabet" mapping (`sVar8` mapped to the actual original byte values `uVar9`). Sets `uVar16 = 1` (a scale tracking variable).
* **Frequencies:** It maintains a dynamically updating array of symbol frequencies (`freqs[i]`) and a `total_freq`. When `total_freq` reaches a threshold (usually `0x4000`), it scales down all frequencies by halving them.
* **Escape Path:** If a symbol is outside the primary alphabet, it falls into an escape branch. The required bit-width `sVar23` is dynamically calculated using the equivalent of `floor(log2(uVar16))` (implemented via the `bsrl` x86 instruction). Extra bits are read from the uniform range coder to reconstruct the unbounded integer.

### 2.3 Predictor (Decorrelator)
OptimFROG's compression efficiency comes from advanced LMS/RLS (Recursive Least Squares) style predictors.
* **State Matrix:** The predictor keeps a history buffer of the decoded samples.
* **Covariance Update:** Every `update_interval` (read from the bitstream), the predictor updates its weights. It computes a covariance matrix `cov_matrix` from the sample history, and uses an LDLT/Cholesky-like factorization (`inst_matrix`) to solve for the optimal LMS weights.
* **Sample Decoding:** `predicted = dot_product(history, weights)`. The value read from the entropy decoder is treated as the **error/residual**. `actual_sample = predicted + error`.
* **Integration:** For Mono audio, the predictor output is mathematically integrated (accumulated). For example, `dest[i] += last_int[j]`. This transforms the difference-coded signal back into the original waveform. (For `sine_mono.ofr`, the integration count is exactly 1).
* **Stereo (`OFR_PredictorStereo` / `FastStereo`):** Stereo prediction involves cross-channel prediction (`cc_count`). The decoder reads flags and weight vectors to predict the Right channel from the Left channel history, exploiting inter-channel correlation.

### 2.4 Post-Processor (`OFR_PostProcessor`)
Takes the predictor output and applies final mapping.
* For basic files, it simply scales by `multiplier` and adds `offset`.
* Negative/complex configurations exist but have not been fully mapped out yet.

---

## 3. Current Decompilation Status

This section is a historical snapshot from early in the project (mono-only, single-file
verification). It's kept for context on how the investigation progressed, but is **no longer
accurate** — see [status.md](status.md) for the current, verified coverage matrix (both
decoder and encoder, mono+stereo, 8/16/24/32-bit, all predictor/entropy/post-processor types,
proven via a bidirectional conformance suite against the reference binaries rather than a single
hand-picked file).

Everything the early to-do list below asked for has since been resolved: predictor
integration/shift is read from the bitstream (not hardcoded), stereo prediction (both the fast
and cascade paths) is bit-exact, all three entropy types and both post-processor types are
implemented, and the block reader handles the full multi-block real-audio case.
