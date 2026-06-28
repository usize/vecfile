#ifndef VECFILE_SCHEMA_H
#define VECFILE_SCHEMA_H

#include "sqlite3.h"

/* Initialize the core tables (namespaces, files, chunks).
   Safe to call on an already-initialized DB. Returns 0 on success. */
int vecfile_schema_init(sqlite3 *db);

/* Create a namespace with frozen config. Creates per-ns FTS5 and vec0 tables.
   Returns the namespace id on success, negative on error. */
int64_t vecfile_ns_create(sqlite3 *db, const char *name,
                          const char *model, int dim,
                          const char *chunk_strategy, int chunk_size,
                          int chunk_overlap, const char *chunk_unit);

/* Look up a namespace by name. Returns id, or -1 if not found. */
int64_t vecfile_ns_lookup(sqlite3 *db, const char *name);

/* Delete a namespace and all its data. Returns 0 on success. */
int vecfile_ns_delete(sqlite3 *db, const char *name);

/* Print namespace list to stdout. Returns 0 on success. */
int vecfile_ns_list(sqlite3 *db);

/* Print namespace info to stdout. Returns 0 on success. */
int vecfile_ns_info(sqlite3 *db, const char *name);

/* Get the namespace's frozen dimension. Returns -1 on error. */
int vecfile_ns_dim(sqlite3 *db, int64_t ns_id);

#endif /* VECFILE_SCHEMA_H */
