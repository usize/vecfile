# Building

## Prerequisites

- macOS (Apple Silicon) or Linux (x86-64 / ARM64)
- `curl`, `unzip` (for toolchain setup)
- ~500 MB disk space for the cosmocc toolchain
- The bge-small-en-v1.5 GGUF model file (35 MB)

No other dependencies. No system compiler, no Python, no CMake, no package manager.

## Setup

```bash
# 1. Get the cosmocc toolchain
make setup

# 2. Download the embedding model
mkdir -p models
curl -L -o models/default.gguf \
  "https://huggingface.co/ggml-org/bge-small-en-v1.5-Q8_0-GGUF/resolve/main/bge-small-en-v1.5-q8_0.gguf"
```

## Build

```bash
make            # build the vecfile binary (~45 MB)
make clean      # remove build artifacts
```

The build compiles ~180 source files (SQLite, sqlite-vec, ggml, llama.cpp,
and our code) and links them with the GGUF model into a single APE binary.
First build takes a few minutes; incremental rebuilds are fast.

## Build flags

The Makefile uses two flag tiers:

| Tier | Flags | Applied to |
|------|-------|-----------|
| Strict | `-Wall -Wextra -Werror` | Our code (`src/`) |
| Relaxed | `-Wno-unused-parameter` | Vendor code (`vendor/`) |

Hot-path ggml files (quantization, tensor ops) are compiled at `-O3`.
Everything else uses `-Os`.

## What the binary contains

| Component | Size | Source |
|-----------|------|--------|
| SQLite 3.49.1 + FTS5 | ~2 MB | `vendor/sqlite/sqlite3.c` |
| sqlite-vec v0.1.7 | ~0.3 MB | `vendor/sqlite-vec/sqlite-vec.c` |
| ggml + llama.cpp | ~8 MB | `vendor/llama.cpp/` |
| bge-small-en-v1.5 Q8_0 | ~35 MB | `models/default.gguf` via zipobj |
| Cosmopolitan Libc | ~0.4 MB | Provided by cosmocc |
| **Total** | **~45 MB** | |
