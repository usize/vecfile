#ifndef VECFILE_QUERY_H
#define VECFILE_QUERY_H

#include "sqlite3.h"
#include "embed.h"

/* Query mode flags */
#define VECFILE_QUERY_HYBRID       0
#define VECFILE_QUERY_SEMANTIC     1
#define VECFILE_QUERY_LEXICAL      2

/* Query options */
typedef struct {
    int mode;          /* HYBRID, SEMANTIC, or LEXICAL */
    int limit;         /* max results (default 10) */
    int pool;          /* candidate pool size (default 50) */
    int rrf_k;         /* RRF constant (default 60) */
    int return_chunks; /* 1 = return chunks, 0 = return parent files */
} vecfile_query_opts;

/* Initialize query options with defaults */
void vecfile_query_opts_default(vecfile_query_opts *opts);

/* Run a hybrid query on a single namespace.
   Prints results to stdout.
   Returns number of results, or negative on error. */
int vecfile_query(sqlite3 *db, int64_t ns_id, vecfile_embedder *emb,
                  const char *query_text, const vecfile_query_opts *opts);

#endif /* VECFILE_QUERY_H */
