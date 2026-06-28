#ifndef VECFILE_INGEST_H
#define VECFILE_INGEST_H

#include "sqlite3.h"
#include "embed.h"

/* Add content to a namespace. Chunks, embeds, and indexes inline.
   path may be NULL (for stdin/literal input).
   meta is optional JSON metadata.
   doc_date is optional unix timestamp (0 = none).
   on_dup: 0 = skip, 1 = replace.
   Returns file id on success, negative on error. */
int64_t vecfile_add(sqlite3 *db, int64_t ns_id, vecfile_embedder *emb,
                    const char *content, int content_len,
                    const char *path, const char *meta,
                    int64_t doc_date, int on_dup);

/* Delete a file by id. CASCADE removes chunks, FTS, vec entries.
   Returns 0 on success. */
int vecfile_delete_by_id(sqlite3 *db, int64_t ns_id, int64_t file_id);

/* Delete a file by path within a namespace.
   Returns 0 on success, -1 if not found. */
int vecfile_delete_by_path(sqlite3 *db, int64_t ns_id, const char *path);

/* Delete all files in a namespace. Returns 0 on success. */
int vecfile_delete_all(sqlite3 *db, int64_t ns_id);

#endif /* VECFILE_INGEST_H */
