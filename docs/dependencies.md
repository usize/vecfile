# Dependencies

vecfile vendors all its dependencies at build time. There are no runtime
dependencies, no dynamic libraries, and no network calls.

## Vendored components

| Component | Version | License | Source | Purpose |
|-----------|---------|---------|--------|---------|
| **cosmocc** | 4.0.2 | ISC | [cosmo.zip](https://cosmo.zip/pub/cosmocc/) | C/C++ toolchain producing APE binaries |
| **SQLite** | 3.49.1 | Public Domain | [sqlite.org](https://sqlite.org) | Database engine, FTS5 full-text search |
| **sqlite-vec** | 0.1.7 | MIT OR Apache-2.0 | [github.com/asg017/sqlite-vec](https://github.com/asg017/sqlite-vec) | vec0 virtual table for vector KNN |
| **llama.cpp** | dbe9c0c | MIT | [github.com/ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) | ggml tensor library + model inference |
| **bge-small-en-v1.5** | Q8_0 | MIT | [huggingface.co/ggml-org](https://huggingface.co/ggml-org/bge-small-en-v1.5-Q8_0-GGUF) | Embedding model (384-dim, 35 MB) |

## License compatibility

All dependencies use permissive licenses. No copyleft (GPL/LGPL/AGPL) anywhere
in the stack. Closed-source redistribution is allowed.

| License | Components | Obligation |
|---------|-----------|-----------|
| Public Domain | SQLite | None |
| ISC | Cosmopolitan Libc | Preserve copyright notice |
| MIT | llama.cpp, ggml, bge-small weights | Preserve copyright notice |
| MIT OR Apache-2.0 | sqlite-vec | Preserve notice (either license) |

## What is NOT a dependency

- **sqlite-lembed** — replaced with a custom ~130-line wrapper (embed.h/cpp)
- **llamafile build system** — we use cosmocc directly, not llamafile's Makefile
- **Python, Node, Go, Rust** — nothing. Pure C/C++ compiled with cosmocc
- **GPU drivers** — CPU-only inference via GGML_CPU_GENERIC
- **Network** — no downloads at runtime, no API calls, no telemetry
