# vecfile — Design Doc

**A single-file, cross-platform, embedding-bundled hybrid search engine over a namespaced corpus.**

Status: draft for coding-harness handoff
Author: Morgan Foster
Audience: an autonomous coding agent (and human reviewers)

---

## 1. One-paragraph summary

`vecfile` is one executable file plus one data file. That's the whole system —
no directory structure, no config files, no runtime. Copy the binary and the
`.db` to a thumb drive, another machine, a server — it just works.

The executable contains everything needed for local hybrid (lexical + semantic)
search: the SQLite engine, a vector-search extension, a GGUF embedding model
**baked into the binary**, the CPU inference code to run it, and built-in
chunking. The data file is an ordinary SQLite database that holds an entire
corpus — raw content stored whole, plus chunked embedding vectors and full-text
indexes — partitioned into **namespaces**.

Content goes in as raw text: piped from stdin, passed as a literal argument, or
read from files for convenience. There is no requirement that content originate
from the filesystem. With one binary and one data file you can index and search
an arbitrarily large corpus, scoped and scaled by namespace. It runs on macOS
(Apple Silicon) and Linux (x86-64, ARM64) with no installer, no runtime, no
server, no network, and no GPU.

It is the thing you reach for when you want "better than BM25" retrieval over a
big personal/operational corpus, shipped as two files, without inheriting the
weight of Chroma/Ollama/a vector-DB server.

**Platform priority: Cosmopolitan/llamafile (native APE) is v1. WebAssembly is
an explicit non-goal for v1, kept reachable via a platform seam (§10).**

---

## 2. Why this doesn't already exist (motivation)

Every ingredient is solved in isolation; the *intersection* is the gap.

| Tool | One file | Weights embedded | Cross-OS single artifact | Hybrid lex+sem | Built-in chunking | Namespaced corpus | Pi / no GPU |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| llamafile | ✅ | ✅ | ✅ | ❌ (chat) | ❌ | ❌ | ✅ |
| sqlite-vec + sqlite-lembed demo | ❌ (+Python,+model) | ❌ | ❌ (.so/.dylib) | partial | ❌ | ❌ | ✅ |
| VittoriaDB | ✅ (per-OS Go) | ❌ (→Ollama) | ❌ | partial | ❌ | partial | ⚠️ |
| LlamaEdge | ❌ (Wasm rt+model) | ❌ | ✅ (Wasm) | ❌ | ❌ | ❌ | ✅ |
| Infinity | ✅ (per-OS) | ❌ | ❌ | ✅ (RRF) | partial | partial | ❌ (heavy C++) |
| **vecfile** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

The novel cell is **APE-packaging of a chunking, namespaced, embedding-driven
hybrid-retrieval engine**. The Cosmopolitan/llamafile world ships "a chatbot
that runs anywhere"; the sqlite-vec world ships "vectors for your existing
app." Nobody sitting in both has welded them into a corpus tool. This project
is that weld.

---

## 3. Goals / Non-goals

### Goals
- **G1.** Single distributable executable, runs on macOS ARM64 + Linux {x86-64, ARM64} (the APE).
- **G2.** Embedding model bundled inside the executable; first run needs no download.
- **G3.** One SQLite data file holds the whole corpus: raw files (whole) + chunks + both indexes + provenance.
- **G4.** Built-in chunking, configured per namespace, applied at ingest.
- **G5.** Hybrid retrieval: FTS5 (BM25) ∪ vector KNN, fused with Reciprocal Rank Fusion.
- **G6.** Namespaces partition the corpus: the unit of config (immutable), of scaling, and of query scope.
- **G7.** CPU-only; runs acceptably on a Raspberry Pi 4/5.
- **G8.** Metadata + date-range filtering on queries.
- **G9.** First-class `migrate` (re-chunk + re-embed from retained raw files; the only way to change frozen config).
- **G10.** Fan-out search across namespaces = repeated single-namespace query + RRF merge, sequential or threaded.
- **G11.** Clean, fully-permissive license story (§9).

### Non-goals
- **N1.** Billion-scale / distributed / sharded search. Brute-force within a namespace; namespacing is the scaling lever; `--ann` is a seam, not v1.
- **N2.** GPU acceleration baked into the binary. (Cosmopolitan static linking precludes pre-linked CUDA/ROCm; CPU is fine for small embedding models.)
- **N3.** WebAssembly / browser build in v1 (§10) — explicitly deferred, seam preserved.
- **N4.** Training, fine-tuning, or rerankers beyond RRF.
- **N5.** A Chroma wire-protocol clone. We borrow ergonomics, not the API.
- **N6.** Concurrency guarantees beyond what SQLite provides (concurrent readers fine; single writer).
- **N7.** Changing a namespace's config in place. Set-once; migrate to change.

---

## 4. Architecture

### 4.1 Component stack (all compiled into one translation-unit set)

```
┌──────────────────────────────────────────────────────────────┐
│ vecfile  (Actually Portable Executable, fat x86-64 + ARM64)    │
│                                                                │
│  main.c   CLI · chunking · RRF fusion · fan-out · migrate      │
│    │                                                           │
│    │  platform.h  (the seam: file I/O, weight load, threads)   │
│    ▼                                                           │
│  ┌──────────┐ ┌────────────┐ ┌──────────────┐                  │
│  │ sqlite3.c│ │ vec0       │ │ lembed       │                  │
│  │ +FTS5    │ │ sqlite-vec │ │ sqlite-lembed│                  │
│  └──────────┘ └────────────┘ └──────────────┘                  │
│  ┌──────────────────────────────────────────────┐             │
│  │ ggml / llama.cpp  (embedding-only path)        │             │
│  └──────────────────────────────────────────────┘             │
│  ┌──────────────────────────────────────────────┐             │
│  │ Cosmopolitan Libc (ISC) — APE loader, syscalls │             │
│  └──────────────────────────────────────────────┘             │
│                                                                │
│  ── zip-appended, read via /zip/… virtual path ──              │
│     /zip/models/default.gguf        (embedded weights)         │
│     /zip/THIRD_PARTY_LICENSES.txt                              │
└──────────────────────────────────────────────────────────────┘
        reads/writes →  corpus.db   (ordinary SQLite file)
```

### 4.2 Key decisions

- **Build base:** fork llamafile's cosmocc build at a pinned commit. It already
  has cosmocc wired up with per-platform CPU SIMD dispatch (tinyBLAS) and the
  embedded-zip mechanism. Biggest single risk-reducer.
- **Trim to embedding-only:** compile only llama.cpp's embedding path. Drop
  sampling, grammars, chat templating, server UI.
- **Extensions compiled in, not dlopen'd:** register `vec0` and `lembed` via
  `sqlite3_auto_extension()` at startup. A static APE loads nothing at runtime.
- **FTS5 enabled** in the amalgamation (`-DSQLITE_ENABLE_FTS5`).
- **Weights in the zip, corpus on disk.** Read-only GGUF zip-appended, mmap'd
  from `/zip/...`. The writable, growing corpus is a normal SQLite file.
- **Everything platform-specific behind `platform.h`** (file I/O, weight load,
  threading) so the native build and a future wasm build are build variants,
  not rewrites (§10).
- **Fat binary via apelink** (x86-64 + ARM64) → native Apple Silicon + Linux.
- **Target platforms:** macOS ARM64 (primary dev), Linux x86-64 and ARM64.
- **Size-optimized** (`-mtiny`, `-Os`).

---

## 5. Data model

Two tiers, partitioned by namespace.

- **Tier 1 — `files`:** the raw content, stored **whole**. Despite the table
  name, a "file" is any unit of text — piped from stdin, passed as a literal
  string, or read from a filesystem path. `path` is optional provenance, not a
  requirement. Retaining raw content is what makes `migrate` (re-chunk/re-embed)
  possible without re-ingestion.
- **Tier 2 — `chunks`:** derived from files via the namespace's chunk config.
  The unit you **embed and search**. A query matches chunks; results join up to
  parent files.

### 5.1 Namespaces (immutable config + scaling/scope unit)

```sql
CREATE TABLE namespaces(
  id             INTEGER PRIMARY KEY,         -- opaque id; used to name per-ns tables
  name           TEXT UNIQUE NOT NULL,        -- human-readable; lives only here
  model          TEXT NOT NULL,               -- e.g. 'bge-small-en-v1.5'
  dim            INTEGER NOT NULL,            -- e.g. 384 (must match the binary's model)
  chunk_strategy TEXT NOT NULL,               -- 'fixed' | 'paragraph' | ...
  chunk_size     INTEGER NOT NULL,            -- tokens (or chars; recorded in unit)
  chunk_unit     TEXT NOT NULL DEFAULT 'token',
  chunk_overlap  INTEGER NOT NULL,
  fts_tokenizer  TEXT NOT NULL DEFAULT 'unicode61',
  schema_version INTEGER NOT NULL DEFAULT 1,
  created_at     INTEGER NOT NULL
  -- config is frozen at creation: set-once, migrate-to-change (§6.7)
);
```

Per-namespace **index tables are named by opaque id**, not by name, so
arbitrary namespace names can't break SQL identifier rules or collide:
`chunks_vec_<id>`, `chunks_fts_<id>`. Created when the namespace is created;
their `float[dim]` is fixed to that namespace's model.

### 5.2 Files and chunks

```sql
CREATE TABLE files(
  id           INTEGER PRIMARY KEY,
  namespace_id INTEGER NOT NULL REFERENCES namespaces(id) ON DELETE CASCADE,
  path         TEXT,                  -- original path/URI (provenance)
  content      TEXT NOT NULL,         -- the ENTIRE raw file
  meta         TEXT,                  -- JSON, arbitrary metadata
  doc_date     INTEGER,               -- optional, indexed, for date-range filters
  sha256       TEXT NOT NULL,         -- dedup + change detection
  bytes        INTEGER,
  chunk_count  INTEGER NOT NULL,      -- total chunks derived from this file
  added_at     INTEGER NOT NULL
);
CREATE INDEX files_ns      ON files(namespace_id);
CREATE INDEX files_ns_date ON files(namespace_id, doc_date);
CREATE INDEX files_sha     ON files(namespace_id, sha256);
CREATE UNIQUE INDEX files_ns_path ON files(namespace_id, path);

CREATE TABLE chunks(
  id           INTEGER PRIMARY KEY,
  namespace_id INTEGER NOT NULL REFERENCES namespaces(id) ON DELETE CASCADE,
  file_id      INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
  ordinal      INTEGER NOT NULL,      -- chunk index within the file
  content      TEXT NOT NULL,         -- the chunk text
  sha256       TEXT NOT NULL,         -- hash of chunk content
  start_off    INTEGER,               -- offset into parent (context expansion)
  end_off      INTEGER
);
CREATE INDEX chunks_file ON chunks(file_id);
CREATE INDEX chunks_ns   ON chunks(namespace_id);
```

### 5.3 Per-namespace index tables (created at namespace creation)

For namespace with id `N` and dimension `D`:

```sql
CREATE VIRTUAL TABLE chunks_fts_N USING fts5(content);  -- rowid = chunks.id
CREATE VIRTUAL TABLE chunks_vec_N USING vec0(
  embedding float[D],
  -- vec0 auxiliary/partition columns let date/category PRUNE the KNN scan
  -- rather than post-filtering brute-force results:
  +doc_date INTEGER          -- auxiliary column, mirrored from files.doc_date
);
```

Rationale for per-namespace tables (vs. one shared vec table + partition key):
makes the immutability guarantee *physical* (a namespace's vectors live in
their own table built for its dimension), lets different namespaces use
different dimensions/models, and makes the scaling story real — a query scans
one namespace's vectors, not the whole corpus.

---

## 6. Core operations

The **single-namespace operation is the only primitive.** Everything else
(fan-out) is orchestration over it.

### 6.1 `ns create`
Insert a `namespaces` row with frozen config; create `chunks_fts_<id>` and
`chunks_vec_<id>` for the chosen dimension. Validate that the requested
`model`/`dim` matches the binary's compiled-in model (§8). Register the lembed
model from the zip path:

```sql
INSERT INTO temp.lembed_models(name, model)
  SELECT 'default', lembed_model_from_file('/zip/models/default.gguf');
```

### 6.2 `add` (ingest, with inline chunking + embedding)
All-in-one: accept content, chunk, embed, and index in a single transaction.
Content can come from anywhere — no filesystem required.

**Input modes (in priority order):**
```
echo "some text" | vecfile add --db mem.db --ns journal -       # stdin
vecfile add --db mem.db --ns journal "literal text here"        # argument
vecfile add --db mem.db --ns notes --file doc.txt               # single file
vecfile add --db mem.db --ns notes --dir ./corpus/              # directory scan
```

**Flow:**
1. Resolve namespace by name → id, load its frozen chunk config.
2. Read content from the input source; compute sha256.
   - For stdin/literal: `path` is NULL unless `--path` is provided explicitly.
   - For `--file`/`--dir`: `path` is set to the source path automatically.
3. **Dedup check:** look up `(namespace_id, sha256)` in `files`.
   - If a match exists and `--on-dup skip` (default): skip silently.
   - If a match exists and `--on-dup replace`: delete the existing file row
     (CASCADE removes its chunks, FTS entries, and vec entries), then proceed.
   - If no match: proceed.
4. Insert the content whole into `files` (with `sha256`, `bytes`, `chunk_count`).
5. **Chunk** the content per namespace config (§7). For each chunk:
   a. Insert into `chunks` (with `ordinal`, `start_off`, `end_off`, `sha256`).
   b. Insert into `chunks_fts_<id>` (rowid = chunk id).
   c. **Embed immediately:** compute `lembed('default', chunk.content)` and
      insert into `chunks_vec_<id>` (rowid = chunk id, with `doc_date`).
6. Update `files.chunk_count` with the final count.
7. Commit the transaction. Content is fully searchable the moment `add` returns.

**Idempotent bulk ingest:** `--dir D` or shell wildcards go through the dedup
check per file, so re-running `add` over the same directory is a no-op for
unchanged content. Updated files (same path, different sha256) are picked up
with `--on-dup replace`.

> Embedding is per-chunk sequential (one `lembed()` call each). Acceptable for
> incremental use; bulk-loading thousands of entries is CPU-bound but correct.

### 6.3 `delete` (remove files or namespaces)
Full CRUD. Deleting a file removes its chunks, FTS entries, and vec entries
via CASCADE. Deleting a namespace removes everything in it.

```
vecfile delete --db PATH --ns NS --id N          # delete one file by id
vecfile delete --db PATH --ns NS --path P        # delete by original path
vecfile delete --db PATH --ns NS --all           # wipe all files in a namespace
vecfile ns delete --db PATH --name NS            # drop the entire namespace
```

All deletes are transactional. `ns delete` also drops the per-namespace
`chunks_fts_<id>` and `chunks_vec_<id>` virtual tables.

### 6.4 `query` (single namespace — the primitive)
Hybrid RRF over one namespace, with optional date/metadata filters, returning
parent files by default:

```sql
WITH lex AS (
  SELECT rowid AS chunk_id, row_number() OVER (ORDER BY rank) AS r
  FROM chunks_fts_<id>
  WHERE chunks_fts_<id> MATCH :q
  LIMIT :pool
),
sem AS (
  SELECT rowid AS chunk_id, row_number() OVER (ORDER BY distance) AS r
  FROM chunks_vec_<id>
  WHERE embedding MATCH :qvec        -- :qvec = lembed('default', :q), computed ONCE
    AND k = :pool
    AND (:since IS NULL OR doc_date >= :since)   -- date prune in the KNN
    AND (:until IS NULL OR doc_date <= :until)
),
-- UNION ALL avoids joining back to the full chunks table:
-- each chunk_id appears at most once per source (lex/sem),
-- and we aggregate the RRF scores per chunk.
combined AS (
  SELECT chunk_id, 1.0/(:rrf_k + r) AS rrf_score FROM lex
  UNION ALL
  SELECT chunk_id, 1.0/(:rrf_k + r) AS rrf_score FROM sem
),
fused AS (
  SELECT chunk_id, SUM(rrf_score) AS score
  FROM combined
  GROUP BY chunk_id
)
SELECT f.id, f.path, f.meta,
       MAX(fused.score) AS score,
       c.start_off AS hit_start, c.end_off AS hit_end
FROM fused
JOIN chunks c ON c.id = fused.chunk_id
JOIN files  f ON f.id = c.file_id
WHERE (:since IS NULL OR f.doc_date >= :since)
  AND (:until IS NULL OR f.doc_date <= :until)
  AND (:json_filter IS NULL OR json_extract(f.meta, :json_path) = :json_val)
GROUP BY f.id
ORDER BY score DESC
LIMIT :limit;
```

Defaults: `:pool=50`, `:rrf_k=60` (TREC default, tuning-insensitive),
`:limit=10`. Flags: `--semantic-only` / `--lexical-only` skip a CTE;
`--chunks` returns bare chunks instead of parent files; `--since/--until` for
dates; `--where '$.k = "v"'` for JSON metadata.

### 6.5 `get` / retrieve whole content
Return `files.content` by id — the whole raw content, intact.

### 6.6 `query --ns a,b,c` / `--all` (fan-out = orchestration)
Not a new query path. The orchestrator:
1. Resolves the target namespaces.
2. **Precondition check:** for semantic fan-out, all targets must share
   `(model, dim)` so one query embedding is comparable across them. Mixed sets
   are refused (or grouped by dim). Lexical-only fan-out has no such
   constraint.
3. **Embed the query once**, reuse `:qvec` across all namespaces.
4. Run the §6.4 primitive per namespace — **sequential queue by default
   (`--workers 1`)**, or a **thread pool (`--workers N>1`)**. SQLite concurrent
   readers are safe; the writer is single.
5. **RRF-merge** the per-namespace result lists into one ranked list (same
   fusion math, third axis = namespace).

Queue is the right default for the Pi / single-core; threading is the win on a
workstation with several large namespaces.

### 6.7 `migrate` (the only way to change frozen config)
Because namespace config is immutable, changing chunk size/strategy/tokenizer
(or re-embedding under a new model in a multi-model future) means:

```
vecfile migrate --from rfcs --to rfcs2 \
  --chunk-size 256 --chunk-overlap 32 --chunk-strategy paragraph
```

Creates a new namespace with new frozen config, then **re-derives** chunks and
vectors from the retained `files.content` of the source namespace — no
re-ingestion. Optionally drops the old namespace on success
(`--drop-source`). This is the second payoff of Tier-1 whole-file storage.

---

## 7. Built-in chunking

Chunking is in-binary and driven entirely by the namespace's frozen config.

- **Default strategy: `fixed`** — fixed-size windows with overlap (default
  ~512 tokens, ~64 overlap). Robust, predictable, no extra in-binary
  dependencies beyond the tokenizer already present for the embedding model.
- **Seam for `paragraph` / sentence-aware** — better retrieval on prose;
  needs a segmenter. Land `fixed` first; expose `chunk_strategy` so smarter
  splitters slot in without schema change.
- **Where it runs:** in the driver (C), at `add` time, writing `chunks`
  transactionally with the parent `files` row. Re-chunking (config change) goes
  through `migrate`, re-deriving from retained raw content.
- **Offsets:** every chunk records `start_off`/`end_off` into the parent so a
  hit can be expanded to surrounding context cheaply at query time.
- **Unit:** record whether `chunk_size` is tokens or chars (`chunk_unit`).
  Token-based is preferred (matches the model's context window); reuse the
  embedding model's tokenizer to count. Tokenization happens at the C level
  via llama.cpp's tokenizer API (already linked for embedding), not via SQL.

---

## 8. Model & binary identity

| Model | Dim | ~Q8 size | License | Notes |
|-------|----:|---------:|---------|-------|
| **bge-small-en-v1.5** | 384 | ~33 MB | MIT | **Default.** Clean provenance, strong retrieval, CPU-fast. |
| all-MiniLM-L6-v2 | 384 | ~24 MB | Apache-2.0 (weights) | Alt build target; training-data discourse, see §9. |

- Model is a **build variable** (`MODEL_GGUF`, `MODEL_NAME`, `MODEL_DIM`)
  flowing into both the zip-append and a compiled-in identity constant.
- **`vecfile_meta` / namespace guard:** a namespace records `model` + `dim`.
  Operations refuse a namespace whose model/dim doesn't match the running
  binary's compiled-in model — preventing silent corruption (e.g. writing
  384-dim vectors into a namespace built for a 768-dim model).
- Estimated artifact size: SQLite+FTS5+vec0 (low single-digit MB) +
  embedding-only ggml (a few MB) + weights (~24–33 MB) ≈ **30–40 MB single
  file**.

---

## 9. Licensing & compliance

All permissive; **no copyleft** anywhere; closed-source redistribution allowed.

| Component | License | Obligation |
|-----------|---------|-----------|
| Cosmopolitan Libc | ISC | preserve notice |
| llamafile (build scaffolding) | Apache-2.0 | notice + LICENSE + note changes |
| llama.cpp / ggml | MIT | preserve notice |
| SQLite | Public Domain | none |
| sqlite-vec | MIT OR Apache-2.0 | preserve notice |
| sqlite-lembed | MIT OR Apache-2.0 | preserve notice |
| bge-small-en-v1.5 weights | MIT | preserve notice |
| (alt) all-MiniLM-L6-v2 weights | Apache-2.0 | preserve notice; see caveat |

**Deliverable:** one `THIRD_PARTY_LICENSES.txt`, zip-appended, dumpable via
`--license`. Extend llamafile's existing notice file with our additions.

**Do-nots:**
- Don't brand as a "llamafile" or imply Mozilla / J. Tunney endorsement
  (Apache-2.0 conveys no trademark rights). Use the project's own name.
- MiniLM caveat: weights are Apache-2.0, but some training datasets (MS MARCO,
  GooAQ) carry murkier redistribution terms. Not a blocker for the published
  weights; bge-small (MIT) sidesteps it and is the default for this reason.
- Engineering summary, not legal advice.

---

## 10. Platform strategy & the WASM seam

**v1 target: Cosmopolitan/llamafile native APE only.** WASM is deferred but
must remain reachable without a rewrite.

Cosmopolitan and WASM are *mutually exclusive packaging strategies* — an APE is
a native executable with a polyglot header, not WASM bytecode; you cannot load
an APE in a browser WASM engine, and cosmocc does not emit `.wasm`. A browser
build is therefore a **separate compile of the same C sources** via
Emscripten/WASI — *not* something the APE produces. Both heavy components
already have proven WASM builds (SQLite official WASM; sqlite-vec advertises
WASM; llama.cpp via wllama), so the codebase is portable; only packaging and
platform glue differ.

To keep that door open, **all platform-specific behavior lives behind
`platform.h`**, with exactly three swap points:

1. **Persistence.** Native: read/write `corpus.db` on disk. Browser:
   IndexedDB/OPFS-backed or in-memory SQLite. The two-tier schema is unchanged;
   only the storage backend differs.
2. **Weight loading.** Native: mmap GGUF from zip-appended `/zip/...`. Browser:
   fetch the `.gguf` over HTTP into WASM memory. **The "weights embedded in the
   binary" property does NOT carry to the browser** — there the model is a
   separate fetched asset. Inherent, not a flaw.
3. **Threading.** Native: OS threads for fan-out `--workers`. Browser: Web
   Workers + `SharedArrayBuffer`/COOP-COEP + WASM SIMD.

**Caveat on the corpus vision in-browser:** WASM linear-memory / tab-RAM limits
make "index all of it" fundamentally a native story. A future browser build
realistically serves small per-page/per-session namespaces, not the whole
archive. WASM is a "same tool, light embedded web use" target, not a
"my entire corpus in a tab" target.

Discipline that buys the browser later = discipline that keeps the native port
clean: no raw `open()`/`mmap()`/thread calls scattered through logic; one
`platform.h` the cosmocc build satisfies now and an Emscripten build could
satisfy later.

---

## 11. CLI surface

```
vecfile ns create  --db PATH --name NS [--model NAME] [--dim N]
                    [--chunk-strategy fixed|paragraph] [--chunk-size N]
                    [--chunk-overlap N] [--chunk-unit token|char]
                    [--fts-tokenizer T]
vecfile ns list     --db PATH
vecfile ns info     --db PATH --name NS
vecfile ns delete   --db PATH --name NS

vecfile add    --db PATH --ns NS [--meta JSON] [--date YYYY-MM-DD]
               [--path NAME] [--on-dup skip|replace]
               ("text" | - | --file F | --dir D)
vecfile delete --db PATH --ns NS (--id N | --path P | --all)
vecfile query  --db PATH (--ns NS | --ns a,b,c | --all)
               [--limit N] [--pool N] [--rrf-k N]
               [--semantic-only|--lexical-only] [--chunks]
               [--since DATE] [--until DATE] [--where '$.k = "v"']
               [--workers N] [--json] "query text"
vecfile get    --db PATH --id N            # whole raw file
vecfile migrate --db PATH --from NS --to NS [chunk/model overrides] [--drop-source]

vecfile model               # compiled-in model id, dim, quantization
vecfile --license           # dump THIRD_PARTY_LICENSES.txt
vecfile --version
```

Conventional exit codes. `--json` everywhere for machine consumption.

---

## 12. Build plan (for the harness)

Phased; each phase ends at a runnable checkpoint. **Cosmopolitan/APE only.**

- **Phase 0 — toolchain.** cosmocc builds a hello-world APE that runs on host (macOS ARM64). Vendor llamafile at a pinned commit.
- **Phase 1 — SQLite core APE.** Compile `sqlite3.c` (`-DSQLITE_ENABLE_FTS5`); open a db, run an FTS5 query. Checkpoint: APE opens DB, FTS works.
- **Phase 2 — vec0 in-binary.** Add `sqlite-vec.c`; register via `sqlite3_auto_extension`; create a vec0 table, insert literal vectors, KNN query — no `.load`.
- **Phase 3 — embedding path (highest risk).** Pull in embedding-only ggml/llama.cpp + `sqlite-lembed.c`. Load GGUF from a plain file path first. Get `lembed('default','hello')` → 384-float blob. Lean on llamafile's exact cosmocc flags. Checkpoint: end-to-end embed in SQL.
- **Phase 4 — bundle weights.** Zip-append the GGUF; switch to `/zip/models/default.gguf`; confirm mmap-from-self. Checkpoint: single file, no external model.
- **Phase 5 — platform.h seam.** Route all file I/O, weight load, threading through `platform.h` with a cosmocc impl. (No wasm impl yet — just the boundary.) Checkpoint: no raw platform calls outside the seam.
- **Phase 6 — namespaces + schema.** `namespaces` table, opaque-id per-ns index tables, model/dim guard, `ns create/list/info/delete`. Checkpoint: create/inspect/delete namespaces.
- **Phase 7 — add (inline chunk + embed) + delete.** Built-in fixed-window chunking from ns config; `add` ingests whole file + chunks + FTS + embeds in one transaction (§6.2). Dedup via sha256 (skip unchanged, replace updated). `delete` removes files/namespaces with CASCADE. Checkpoint: ingest a dir, re-run is no-op, delete works.
- **Phase 8 — query primitive + RRF + filters.** §6.4 single-ns hybrid query with date/JSON filters, `--chunks`, parent-file return. Checkpoint: "better than BM25" fixture passes (A2).
- **Phase 9 — fan-out + workers + migrate.** `--ns a,b,c`/`--all` with embed-once-reuse, same-`(model,dim)` precondition, queue + thread pool; first-class `migrate`. Checkpoint: multi-ns search + a real migration.
- **Phase 10 — fat binary + polish.** apelink x86-64+ARM64; `--license`, `model`, version, `-mtiny -Os`. Run the §13 matrix.

---

## 13. Test matrix & acceptance

Same binary on every cell; identical results expected.

| OS | x86-64 | ARM64 |
|----|:---:|:---:|
| macOS | — | ✓ (Apple Silicon, primary dev) |
| Linux | ✓ | ✓ |

**Acceptance tests:**
- **A1.** `ns create`→`add`(≥1k files)→`query` returns sane ranked parent files. No separate index step.
- **A2.** *(the core claim)* Hybrid beats lexical-only on a paraphrase query; lexical-only catches an exact code/ID token semantic-only misses. Encode as a fixture.
- **A3.** A `corpus.db` built on Linux opens and queries correctly on macOS (DB portability).
- **A4.** Dedup: re-running `add` over the same directory is a no-op (sha256 match → skip). Running with `--on-dup replace` on a changed file drops and re-ingests it.
- **A5.** `--license` dumps complete notices; nothing missing.
- **A6.** Model/dim guard: a namespace tagged model X refuses a binary built with model Y, rather than corrupting.
- **A7.** Built-in chunking: a long file produces N overlapping chunks with correct offsets; a query returns the parent file with the right hit offset.
- **A8.** Immutability: attempting to change a namespace's chunk config in place fails; `migrate` to a new namespace succeeds and re-derives from retained raw files with no re-ingestion.
- **A9.** Fan-out: `--ns a,b` returns a correctly RRF-merged list; mixed-dimension semantic fan-out is refused with a clear error; `--workers 4` matches `--workers 1` results.
- **A10.** Delete: `delete --id`, `delete --path`, `delete --all`, and `ns delete` all correctly remove files, chunks, FTS entries, and vec entries.
- **A11.** Binary size within target (≤ ~40 MB with bge-small).

---

## 14. Risks & open questions

- **R1 (highest): ggml under cosmocc.** Mitigation: fork llamafile's exact build flags. Target macOS ARM64 + Linux x86-64/ARM64 only.
- **R2: sqlite-lembed maturity / no batching.** Acceptable for incremental ingest; document reindex cost; possible upstream batching contribution.
- **R3: per-namespace table proliferation.** Many namespaces = many vtables. Fine for tens–hundreds; if thousands of namespaces are expected, revisit (shared table + partition key as a fallback mode).
- **R4: APE execution friction.** Gatekeeper on macOS; binfmt_misc on some Linux distros. Ship a short "first run" note. UX/docs, not code.
- **Q1:** Light ANN (sqlite-vec DiskANN/IVF) as opt-in for very large single namespaces? Lean: namespacing first; `--ann` seam; not v1.
- **Q2:** Default chunk size/overlap and token-vs-char default — confirm 512/64 tokens.
- **Q3:** Multi-model-in-one-binary (runtime-selectable) vs one-model-per-build? Lean: one-per-build for v1; the namespace model/dim guard already anticipates multi-model.
- **Q4:** `serve` in v1 or later? Lean: optional, Phase 10, behind the platform seam.

---

## 15. Naming

Working name `vecfile`. Must **not** be "*llamafile" or imply Mozilla
endorsement (§9). Alternatives: `vsearch`, `embfile`, `seekstone`, `grepvec`,
`corpusfile`. Pick before first public artifact.

