#include "schema.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

int vecfile_schema_init(sqlite3 *db) {
    const char *sql =
        "PRAGMA foreign_keys = ON;"
        "CREATE TABLE IF NOT EXISTS namespaces("
        "  id             INTEGER PRIMARY KEY,"
        "  name           TEXT UNIQUE NOT NULL,"
        "  model          TEXT NOT NULL,"
        "  dim            INTEGER NOT NULL,"
        "  chunk_strategy TEXT NOT NULL,"
        "  chunk_size     INTEGER NOT NULL,"
        "  chunk_unit     TEXT NOT NULL DEFAULT 'token',"
        "  chunk_overlap  INTEGER NOT NULL,"
        "  fts_tokenizer  TEXT NOT NULL DEFAULT 'unicode61',"
        "  schema_version INTEGER NOT NULL DEFAULT 1,"
        "  created_at     INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS files("
        "  id           INTEGER PRIMARY KEY,"
        "  namespace_id INTEGER NOT NULL REFERENCES namespaces(id) ON DELETE CASCADE,"
        "  path         TEXT,"
        "  content      TEXT NOT NULL,"
        "  meta         TEXT,"
        "  doc_date     INTEGER,"
        "  sha256       TEXT NOT NULL,"
        "  bytes        INTEGER,"
        "  chunk_count  INTEGER NOT NULL DEFAULT 0,"
        "  added_at     INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS files_ns      ON files(namespace_id);"
        "CREATE INDEX IF NOT EXISTS files_ns_date ON files(namespace_id, doc_date);"
        "CREATE INDEX IF NOT EXISTS files_sha     ON files(namespace_id, sha256);"
        "CREATE TABLE IF NOT EXISTS chunks("
        "  id           INTEGER PRIMARY KEY,"
        "  namespace_id INTEGER NOT NULL REFERENCES namespaces(id) ON DELETE CASCADE,"
        "  file_id      INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,"
        "  ordinal      INTEGER NOT NULL,"
        "  content      TEXT NOT NULL,"
        "  sha256       TEXT NOT NULL,"
        "  start_off    INTEGER,"
        "  end_off      INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS chunks_file ON chunks(file_id);"
        "CREATE INDEX IF NOT EXISTS chunks_ns   ON chunks(namespace_id);";

    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "schema init error: %s\n", err);
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

int64_t vecfile_ns_create(sqlite3 *db, const char *name,
                          const char *model, int dim,
                          const char *chunk_strategy, int chunk_size,
                          int chunk_overlap, const char *chunk_unit) {
    /* Insert namespace row */
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO namespaces(name, model, dim, chunk_strategy, chunk_size,"
        " chunk_overlap, chunk_unit, created_at)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ns create prepare: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, dim);
    sqlite3_bind_text(stmt, 4, chunk_strategy, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, chunk_size);
    sqlite3_bind_int(stmt, 6, chunk_overlap);
    sqlite3_bind_text(stmt, 7, chunk_unit, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, vecfile_now_unix());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "ns create: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int64_t ns_id = sqlite3_last_insert_rowid(db);

    /* Create per-namespace FTS5 table */
    char sql[256];
    snprintf(sql, sizeof(sql),
        "CREATE VIRTUAL TABLE chunks_fts_%lld USING fts5(content)",
        (long long)ns_id);
    char *err = NULL;
    rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ns create fts: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    /* Create per-namespace vec0 table */
    snprintf(sql, sizeof(sql),
        "CREATE VIRTUAL TABLE chunks_vec_%lld USING vec0("
        "embedding float[%d],"
        "+doc_date INTEGER"
        ")", (long long)ns_id, dim);
    rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ns create vec: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    return ns_id;
}

int64_t vecfile_ns_lookup(sqlite3 *db, const char *name) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT id FROM namespaces WHERE name = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    int64_t id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

int vecfile_ns_dim(sqlite3 *db, int64_t ns_id) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT dim FROM namespaces WHERE id = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int64(stmt, 1, ns_id);
    int dim = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        dim = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return dim;
}

int vecfile_ns_delete(sqlite3 *db, const char *name) {
    int64_t ns_id = vecfile_ns_lookup(db, name);
    if (ns_id < 0) {
        fprintf(stderr, "namespace '%s' not found\n", name);
        return -1;
    }

    char sql[128];
    char *err = NULL;

    /* Drop per-namespace virtual tables first */
    snprintf(sql, sizeof(sql), "DROP TABLE IF EXISTS chunks_fts_%lld",
             (long long)ns_id);
    sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err) { sqlite3_free(err); err = NULL; }

    snprintf(sql, sizeof(sql), "DROP TABLE IF EXISTS chunks_vec_%lld",
             (long long)ns_id);
    sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err) { sqlite3_free(err); err = NULL; }

    /* Delete namespace row — CASCADE removes files and chunks */
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "DELETE FROM namespaces WHERE id = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int64(stmt, 1, ns_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int vecfile_ns_list(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT name, model, dim, chunk_strategy, chunk_size, chunk_overlap,"
        " (SELECT COUNT(*) FROM files WHERE namespace_id = namespaces.id)"
        " FROM namespaces ORDER BY name", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ns list: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%-20s  model=%-24s  dim=%-4d  chunk=%s/%d/%d  files=%d\n",
            sqlite3_column_text(stmt, 0),
            sqlite3_column_text(stmt, 1),
            sqlite3_column_int(stmt, 2),
            sqlite3_column_text(stmt, 3),
            sqlite3_column_int(stmt, 4),
            sqlite3_column_int(stmt, 5),
            sqlite3_column_int(stmt, 6));
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) printf("(no namespaces)\n");
    return 0;
}

int vecfile_ns_info(sqlite3 *db, const char *name) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT id, name, model, dim, chunk_strategy, chunk_size, chunk_unit,"
        " chunk_overlap, fts_tokenizer, schema_version, created_at,"
        " (SELECT COUNT(*) FROM files WHERE namespace_id = namespaces.id),"
        " (SELECT COUNT(*) FROM chunks WHERE namespace_id = namespaces.id)"
        " FROM namespaces WHERE name = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "ns info: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        fprintf(stderr, "namespace '%s' not found\n", name);
        sqlite3_finalize(stmt);
        return -1;
    }

    printf("Namespace: %s\n", sqlite3_column_text(stmt, 1));
    printf("  id:             %lld\n", (long long)sqlite3_column_int64(stmt, 0));
    printf("  model:          %s\n", sqlite3_column_text(stmt, 2));
    printf("  dim:            %d\n", sqlite3_column_int(stmt, 3));
    printf("  chunk_strategy: %s\n", sqlite3_column_text(stmt, 4));
    printf("  chunk_size:     %d %s\n", sqlite3_column_int(stmt, 5),
           sqlite3_column_text(stmt, 6));
    printf("  chunk_overlap:  %d\n", sqlite3_column_int(stmt, 7));
    printf("  fts_tokenizer:  %s\n", sqlite3_column_text(stmt, 8));
    printf("  files:          %d\n", sqlite3_column_int(stmt, 11));
    printf("  chunks:         %d\n", sqlite3_column_int(stmt, 12));

    sqlite3_finalize(stmt);
    return 0;
}
