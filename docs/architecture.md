# Architecture

vecfile is a single Actually Portable Executable (APE) that bundles a complete
hybrid search engine: SQLite with FTS5, sqlite-vec for vector KNN, and
llama.cpp for CPU embedding inference, with a GGUF model baked into the binary.

## Source tree

```
vecfile/
├── src/
│   ├── main.c          CLI entry point, argument parsing, command dispatch
│   ├── schema.h/c      Namespace + table management (create, list, delete)
│   ├── ingest.h/c      Content ingestion (chunking, embedding, dedup, CRUD)
│   ├── query.h/c       Hybrid RRF query engine (lexical + semantic fusion)
│   ├── embed.h/cpp     Thin C wrapper over llama.cpp embedding inference
│   └── platform.h      Platform abstraction seam (file I/O, threading)
│
├── vendor/
│   ├── sqlite/         SQLite 3.49.1 amalgamation (sqlite3.c, sqlite3.h)
│   ├── sqlite-vec/     sqlite-vec v0.1.7 amalgamation (sqlite-vec.c/h)
│   └── llama.cpp/      llama.cpp at commit dbe9c0c (llamafile's pin)
│       ├── include/    Public headers (llama.h)
│       ├── ggml/       Tensor library — CPU backend, quantization
│       │   ├── include/  Public ggml headers
│       │   └── src/      Core source + ggml-cpu/ backend
│       └── src/        llama.cpp core + src/models/ (129 architectures)
│
├── models/
│   └── default.gguf    bge-small-en-v1.5 Q8_0 (35 MB, not in git)
│
├── Makefile            Build system (cosmocc 4.0.2)
├── design.md           Full design document
└── .cosmocc/           Cosmopolitan toolchain (not in git)
```

## Component stack

```
┌─────────────────────────────────────────────────────────┐
│  vecfile (APE, ~45 MB)                                  │
│                                                         │
│  main.c ─── schema.c ─── ingest.c ─── query.c          │
│      │                                                  │
│      │  platform.h (seam)                               │
│      ▼                                                  │
│  ┌──────────┐  ┌────────────┐  ┌──────────────┐        │
│  │ sqlite3.c│  │ sqlite-vec │  │  embed.cpp   │        │
│  │ + FTS5   │  │   (vec0)   │  │ (llama.cpp)  │        │
│  └──────────┘  └────────────┘  └──────────────┘        │
│  ┌──────────────────────────────────────────────┐       │
│  │  ggml / llama.cpp  (CPU embedding inference) │       │
│  └──────────────────────────────────────────────┘       │
│  ┌──────────────────────────────────────────────┐       │
│  │  Cosmopolitan Libc (ISC) — APE, syscalls     │       │
│  └──────────────────────────────────────────────┘       │
│                                                         │
│  /zip/models/default.gguf  (embedded via zipobj)        │
└─────────────────────────────────────────────────────────┘
       reads/writes → corpus.db (ordinary SQLite file)
```

## Data flow

**Ingest (`add`):**
```
content → sha256 dedup check → chunk (fixed window) → embed (llama.cpp)
  → INSERT files + chunks + chunks_fts_N + chunks_vec_N  (one transaction)
```

**Query:**
```
query text → embed query → FTS5 BM25 candidates + vec0 KNN candidates
  → UNION ALL → RRF score aggregation → GROUP BY file → ranked results
```

**Context retrieval (`get --chunk N -C 2`):**
```
chunk_id → find file_id + ordinal → SELECT neighboring chunks by ordinal range
```

## Key design decisions

- **No sqlite-lembed.** Replaced with a ~130-line C++ wrapper (embed.h/cpp)
  calling llama.cpp directly. Avoids an unmaintained dependency.
- **GGML_CPU_GENERIC.** No architecture-specific SIMD. Compiles cleanly under
  cosmocc with zero patches. Performance is acceptable for bge-small (33M params).
- **Character-based chunking.** Simpler than token-based for v1. The chunk_unit
  field in the schema supports switching to token-based later.
- **Per-namespace virtual tables.** Each namespace gets its own `chunks_fts_N`
  and `chunks_vec_N`. Keeps dimensions isolated and queries scoped.
- **SHA256 dedup at the content level.** Makes wildcard re-runs instant.
