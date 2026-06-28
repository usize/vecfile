#ifndef VECFILE_PLATFORM_H
#define VECFILE_PLATFORM_H

/*
 * Platform abstraction seam.
 *
 * All platform-specific behavior routes through here so the native
 * Cosmopolitan build and a future WASM build are build variants,
 * not rewrites. For v1, these are thin wrappers over standard libc
 * calls that Cosmopolitan handles.
 *
 * Swap points:
 *   1. Persistence   — file I/O for corpus.db (disk vs IndexedDB/OPFS)
 *   2. Weight loading — /zip/ mmap vs HTTP fetch into WASM memory
 *   3. Threading      — OS threads vs Web Workers
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ── Weight loading ──────────────────────────────────────────── */

/* Path to the bundled model in the binary's zip section */
#define VECFILE_BUNDLED_MODEL_PATH "/zip/models/default.gguf"

/* ── Time ────────────────────────────────────────────────────── */

static inline int64_t vecfile_now_unix(void) {
    return (int64_t)time(NULL);
}

/* ── Threading (placeholder for fan-out workers) ─────────────── */

/* v1: sequential only. Fan-out threading is Phase 9. */

#endif /* VECFILE_PLATFORM_H */
