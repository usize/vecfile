#include "query.h"
#include "schema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vecfile_query_opts_default(vecfile_query_opts *opts) {
    opts->mode = VECFILE_QUERY_HYBRID;
    opts->limit = 10;
    opts->pool = 50;
    opts->rrf_k = 60;
    opts->return_chunks = 0;
}

int vecfile_query(sqlite3 *db, int64_t ns_id, vecfile_embedder *emb,
                  const char *query_text, const vecfile_query_opts *opts) {
    int dim = vecfile_ns_dim(db, ns_id);
    if (dim < 0) {
        fprintf(stderr, "query: namespace not found\n");
        return -1;
    }

    /* Embed the query (for semantic or hybrid) */
    float *qvec = NULL;
    if (opts->mode != VECFILE_QUERY_LEXICAL) {
        qvec = (float *)malloc(dim * sizeof(float));
        int rc = vecfile_embed(emb, query_text, (int)strlen(query_text),
                               qvec, dim);
        if (rc < 0) {
            fprintf(stderr, "query: embedding failed: %d\n", rc);
            free(qvec);
            return -1;
        }
    }

    /*
     * Build the hybrid RRF query.
     * Uses UNION ALL on lex and sem CTEs, aggregates per chunk.
     */
    char sql[2048];
    int pos = 0;

    if (opts->mode == VECFILE_QUERY_HYBRID || opts->mode == VECFILE_QUERY_LEXICAL) {
        pos += snprintf(sql + pos, sizeof(sql) - pos,
            "WITH lex AS ("
            "  SELECT rowid AS chunk_id, row_number() OVER (ORDER BY rank) AS r"
            "  FROM chunks_fts_%lld"
            "  WHERE chunks_fts_%lld MATCH ?1"
            "  LIMIT %d"
            ")",
            (long long)ns_id, (long long)ns_id, opts->pool);
    }

    if (opts->mode == VECFILE_QUERY_HYBRID) {
        pos += snprintf(sql + pos, sizeof(sql) - pos, ", ");
    }

    if (opts->mode == VECFILE_QUERY_HYBRID || opts->mode == VECFILE_QUERY_SEMANTIC) {
        if (opts->mode == VECFILE_QUERY_SEMANTIC) {
            pos += snprintf(sql + pos, sizeof(sql) - pos, "WITH ");
        }
        pos += snprintf(sql + pos, sizeof(sql) - pos,
            "sem AS ("
            "  SELECT rowid AS chunk_id, row_number() OVER (ORDER BY distance) AS r"
            "  FROM chunks_vec_%lld"
            "  WHERE embedding MATCH ?2"
            "  AND k = %d"
            ")",
            (long long)ns_id, opts->pool);
    }

    /* Combined CTE */
    pos += snprintf(sql + pos, sizeof(sql) - pos, ", combined AS (");
    if (opts->mode == VECFILE_QUERY_HYBRID) {
        pos += snprintf(sql + pos, sizeof(sql) - pos,
            "SELECT chunk_id, 1.0/(%d + r) AS rrf_score FROM lex"
            " UNION ALL "
            "SELECT chunk_id, 1.0/(%d + r) AS rrf_score FROM sem",
            opts->rrf_k, opts->rrf_k);
    } else if (opts->mode == VECFILE_QUERY_LEXICAL) {
        pos += snprintf(sql + pos, sizeof(sql) - pos,
            "SELECT chunk_id, 1.0/(%d + r) AS rrf_score FROM lex",
            opts->rrf_k);
    } else {
        pos += snprintf(sql + pos, sizeof(sql) - pos,
            "SELECT chunk_id, 1.0/(%d + r) AS rrf_score FROM sem",
            opts->rrf_k);
    }

    pos += snprintf(sql + pos, sizeof(sql) - pos,
        "), fused AS ("
        "  SELECT chunk_id, SUM(rrf_score) AS score"
        "  FROM combined GROUP BY chunk_id"
        ")");

    if (opts->return_chunks) {
        pos += snprintf(sql + pos, sizeof(sql) - pos,
            " SELECT c.id, c.file_id, c.ordinal, c.content,"
            " fused.score, c.start_off, c.end_off"
            " FROM fused"
            " JOIN chunks c ON c.id = fused.chunk_id"
            " ORDER BY fused.score DESC"
            " LIMIT %d", opts->limit);
    } else {
        pos += snprintf(sql + pos, sizeof(sql) - pos,
            " SELECT f.id, f.path, f.meta,"
            " MAX(fused.score) AS score,"
            " c.start_off AS hit_start, c.end_off AS hit_end"
            " FROM fused"
            " JOIN chunks c ON c.id = fused.chunk_id"
            " JOIN files f ON f.id = c.file_id"
            " GROUP BY f.id"
            " ORDER BY score DESC"
            " LIMIT %d", opts->limit);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "query prepare: %s\nSQL: %s\n", sqlite3_errmsg(db), sql);
        free(qvec);
        return -1;
    }

    /* Bind query text for FTS */
    if (opts->mode != VECFILE_QUERY_SEMANTIC) {
        sqlite3_bind_text(stmt, 1, query_text, -1, SQLITE_TRANSIENT);
    }
    /* Bind query vector for semantic */
    if (opts->mode != VECFILE_QUERY_LEXICAL && qvec) {
        sqlite3_bind_blob(stmt, 2, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
        if (opts->return_chunks) {
            printf("[%d] chunk_id=%lld  file_id=%lld  ordinal=%d  score=%.6f\n",
                count,
                (long long)sqlite3_column_int64(stmt, 0),
                (long long)sqlite3_column_int64(stmt, 1),
                sqlite3_column_int(stmt, 2),
                sqlite3_column_double(stmt, 4));
            const char *text = (const char *)sqlite3_column_text(stmt, 3);
            int len = sqlite3_column_bytes(stmt, 3);
            if (len > 200) {
                printf("    %.200s...\n\n", text);
            } else {
                printf("    %s\n\n", text);
            }
        } else {
            const char *path = (const char *)sqlite3_column_text(stmt, 1);
            printf("[%d] file_id=%lld  score=%.6f  path=%s\n",
                count,
                (long long)sqlite3_column_int64(stmt, 0),
                sqlite3_column_double(stmt, 3),
                path ? path : "(stdin)");
        }
    }

    sqlite3_finalize(stmt);
    free(qvec);
    return count;
}
