# Usage

## Quick start

```bash
# Create a namespace
vecfile ns create --db mem.db --name notes

# Add content (text, files, wildcards)
vecfile add --db mem.db --ns notes "Today I learned about hybrid search"
vecfile add --db mem.db --ns notes --file paper.txt
vecfile add --db mem.db --ns notes ~/docs/*.md

# Search
vecfile query --db mem.db --ns notes "how does ranking work"
```

## Namespaces

A namespace is an isolated partition with frozen configuration. Each namespace
gets its own FTS5 and vector tables.

```bash
vecfile ns create --db PATH --name NS [--chunk-size 512] [--chunk-overlap 64]
vecfile ns list   --db PATH
vecfile ns info   --db PATH --name NS
vecfile ns delete --db PATH --name NS
```

Chunk size and overlap are set at creation and cannot be changed. To use
different settings, create a new namespace.

## Adding content

Content can come from four sources:

```bash
# Literal text
vecfile add --db PATH --ns NS "some text to remember"

# Stdin
echo "piped text" | vecfile add --db PATH --ns NS -

# Single file
vecfile add --db PATH --ns NS --file document.txt

# Multiple files / wildcards (shell expands the glob)
vecfile add --db PATH --ns NS ~/notes/*.md /tmp/logs/*.txt
```

**Options:**
- `--tag NAME` — label stdin/literal content with a name for later retrieval
- `--meta JSON` — attach arbitrary JSON metadata
- `--on-dup skip|replace` — what to do if sha256 matches existing content
  (default: skip)

**Dedup:** content is hashed with SHA256. Re-running the same `add` command
is a no-op for unchanged files — safe to use in a cron job or script.

## Querying

```bash
# Hybrid search (FTS5 + vector KNN, fused with RRF)
vecfile query --db PATH --ns NS "query text"

# Semantic only (vector similarity, no keyword matching)
vecfile query --db PATH --ns NS --semantic-only "conceptual question"

# Lexical only (BM25 keyword matching, no embedding)
vecfile query --db PATH --ns NS --lexical-only "exact_identifier"

# Return individual chunks instead of parent files
vecfile query --db PATH --ns NS --chunks "query text"
```

**Options:**
- `--limit N` — max results (default 10)
- `--pool N` — candidate pool size before RRF fusion (default 50)
- `--rrf-k N` — RRF smoothing constant (default 60)
- `--chunks` — return chunk-level results with content previews

## Retrieving content

```bash
# Get full content by file id
vecfile get --db PATH --id 42

# Get full content by tag
vecfile get --db PATH --tag "daily-notes-june-27"

# Get a single chunk
vecfile get --db PATH --chunk 87

# Get a chunk with surrounding context (like grep -C)
vecfile get --db PATH --chunk 87 -C 2
```

The `-C N` flag is designed for LLM consumption: query with `--chunks` to find
relevant chunk_ids, then expand context around a hit without loading the entire
source file.

## Deleting content

```bash
# Delete a file by id
vecfile delete --db PATH --ns NS --id 42

# Delete a file by path/tag
vecfile delete --db PATH --ns NS --path "document.txt"

# Delete all files in a namespace
vecfile delete --db PATH --ns NS --all
```

## Inspecting the binary

```bash
vecfile --version    # version + SQLite/sqlite-vec versions
vecfile model        # compiled-in model name, dimension, quantization
```

## LLM retrieval workflow

An LLM using vecfile as a memory layer would:

```bash
# 1. Find relevant chunks
vecfile query --db mem.db --ns context --chunks "topic of interest"

# 2. Expand context around the best hit
vecfile get --db mem.db --chunk 87 -C 2

# 3. Or grab the full source if needed
vecfile get --db mem.db --tag "meeting-notes-2024-03"
```
