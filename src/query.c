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

/* ── Result collection for cross-namespace merge ────────────── */

typedef struct {
    double score;
    int64_t id;         /* chunk_id or file_id */
    int64_t file_id;    /* only for chunk results */
    int ordinal;        /* only for chunk results */
    char *text;         /* chunk content or file path */
    char *path;         /* file path (for file results) */
    char *ns_name;      /* namespace name */
    char *meta;         /* file metadata */
    int start_off;
    int end_off;
} query_result;

typedef struct {
    query_result *items;
    int count;
    int cap;
} result_list;

static void result_list_init(result_list *rl) {
    rl->cap = 64;
    rl->count = 0;
    rl->items = (query_result *)calloc(rl->cap, sizeof(query_result));
}

static query_result *result_list_add(result_list *rl) {
    if (rl->count == rl->cap) {
        rl->cap *= 2;
        rl->items = (query_result *)realloc(rl->items, rl->cap * sizeof(query_result));
    }
    query_result *r = &rl->items[rl->count++];
    memset(r, 0, sizeof(*r));
    return r;
}

static void result_list_free(result_list *rl) {
    for (int i = 0; i < rl->count; i++) {
        free(rl->items[i].text);
        free(rl->items[i].path);
        free(rl->items[i].ns_name);
        free(rl->items[i].meta);
    }
    free(rl->items);
}

static int cmp_score_desc(const void *a, const void *b) {
    double sa = ((const query_result *)a)->score;
    double sb = ((const query_result *)b)->score;
    if (sb > sa) return 1;
    if (sb < sa) return -1;
    return 0;
}

static char *safe_strdup(const char *s) {
    return s ? strdup(s) : NULL;
}

/* ── Core query (collects into result_list) ─────────────────── */

static int query_into(sqlite3 *db, int64_t ns_id, const char *ns_name,
                      const float *qvec, int dim,
                      const char *query_text, const vecfile_query_opts *opts,
                      result_list *results) {
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
            " LIMIT %d", opts->pool);  /* collect pool, trim after merge */
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
            " LIMIT %d", opts->pool);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "query prepare: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    if (opts->mode != VECFILE_QUERY_SEMANTIC) {
        sqlite3_bind_text(stmt, 1, query_text, -1, SQLITE_TRANSIENT);
    }
    if (opts->mode != VECFILE_QUERY_LEXICAL && qvec) {
        sqlite3_bind_blob(stmt, 2, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        query_result *r = result_list_add(results);
        r->ns_name = safe_strdup(ns_name);

        if (opts->return_chunks) {
            r->id = sqlite3_column_int64(stmt, 0);
            r->file_id = sqlite3_column_int64(stmt, 1);
            r->ordinal = sqlite3_column_int(stmt, 2);
            r->text = safe_strdup((const char *)sqlite3_column_text(stmt, 3));
            r->score = sqlite3_column_double(stmt, 4);
            r->start_off = sqlite3_column_int(stmt, 5);
            r->end_off = sqlite3_column_int(stmt, 6);
        } else {
            r->id = sqlite3_column_int64(stmt, 0);
            r->path = safe_strdup((const char *)sqlite3_column_text(stmt, 1));
            r->meta = safe_strdup((const char *)sqlite3_column_text(stmt, 2));
            r->score = sqlite3_column_double(stmt, 3);
            r->start_off = sqlite3_column_int(stmt, 4);
            r->end_off = sqlite3_column_int(stmt, 5);
        }
    }

    sqlite3_finalize(stmt);
    return 0;
}

/* ── Print results ──────────────────────────────────────────── */

static int print_results(result_list *results, const vecfile_query_opts *opts,
                         int show_ns) {
    qsort(results->items, results->count, sizeof(query_result), cmp_score_desc);

    int limit = opts->limit < results->count ? opts->limit : results->count;
    for (int i = 0; i < limit; i++) {
        query_result *r = &results->items[i];
        if (opts->return_chunks) {
            if (show_ns) {
                printf("[%d] ns=%s  chunk_id=%lld  file_id=%lld  ordinal=%d  score=%.6f\n",
                    i + 1, r->ns_name,
                    (long long)r->id, (long long)r->file_id,
                    r->ordinal, r->score);
            } else {
                printf("[%d] chunk_id=%lld  file_id=%lld  ordinal=%d  score=%.6f\n",
                    i + 1,
                    (long long)r->id, (long long)r->file_id,
                    r->ordinal, r->score);
            }
            if (r->text) {
                int len = (int)strlen(r->text);
                if (len > 200) {
                    printf("    %.200s...\n\n", r->text);
                } else {
                    printf("    %s\n\n", r->text);
                }
            }
        } else {
            if (show_ns) {
                printf("[%d] ns=%s  file_id=%lld  score=%.6f  path=%s\n",
                    i + 1, r->ns_name,
                    (long long)r->id, r->score,
                    r->path ? r->path : "(stdin)");
            } else {
                printf("[%d] file_id=%lld  score=%.6f  path=%s\n",
                    i + 1,
                    (long long)r->id, r->score,
                    r->path ? r->path : "(stdin)");
            }
        }
    }

    return limit;
}

/* ── Public API ─────────────────────────────────────────────── */

int vecfile_query(sqlite3 *db, int64_t ns_id, vecfile_embedder *emb,
                  const char *query_text, const vecfile_query_opts *opts) {
    int dim = vecfile_ns_dim(db, ns_id);
    if (dim < 0) {
        fprintf(stderr, "query: namespace not found\n");
        return -1;
    }

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

    result_list results;
    result_list_init(&results);

    int rc = query_into(db, ns_id, NULL, qvec, dim, query_text, opts, &results);
    free(qvec);

    if (rc < 0) {
        result_list_free(&results);
        return -1;
    }

    int n = print_results(&results, opts, 0);
    result_list_free(&results);
    return n;
}

int vecfile_query_all(sqlite3 *db, vecfile_embedder *emb,
                      const char *query_text, const vecfile_query_opts *opts) {
    /* Get all namespaces */
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT id, name, dim FROM namespaces ORDER BY name",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "query_all: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    /* Collect namespace list first (can't nest queries) */
    typedef struct { int64_t id; char *name; int dim; } ns_entry;
    ns_entry *nses = NULL;
    int n_ns = 0, cap_ns = 8;
    nses = (ns_entry *)malloc(cap_ns * sizeof(ns_entry));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (n_ns == cap_ns) {
            cap_ns *= 2;
            nses = (ns_entry *)realloc(nses, cap_ns * sizeof(ns_entry));
        }
        nses[n_ns].id = sqlite3_column_int64(stmt, 0);
        nses[n_ns].name = strdup((const char *)sqlite3_column_text(stmt, 1));
        nses[n_ns].dim = sqlite3_column_int(stmt, 2);
        n_ns++;
    }
    sqlite3_finalize(stmt);

    if (n_ns == 0) {
        fprintf(stderr, "no namespaces found\n");
        free(nses);
        return -1;
    }

    /* Embed the query once (all namespaces must share dim for semantic) */
    float *qvec = NULL;
    int dim = nses[0].dim;

    if (opts->mode != VECFILE_QUERY_LEXICAL) {
        /* Verify all namespaces share the same dimension */
        for (int i = 1; i < n_ns; i++) {
            if (nses[i].dim != dim) {
                fprintf(stderr, "query_all: mixed dimensions (%d vs %d) — "
                        "use --ns to query specific namespaces\n",
                        dim, nses[i].dim);
                for (int j = 0; j < n_ns; j++) free(nses[j].name);
                free(nses);
                return -1;
            }
        }

        qvec = (float *)malloc(dim * sizeof(float));
        rc = vecfile_embed(emb, query_text, (int)strlen(query_text), qvec, dim);
        if (rc < 0) {
            fprintf(stderr, "query: embedding failed: %d\n", rc);
            free(qvec);
            for (int j = 0; j < n_ns; j++) free(nses[j].name);
            free(nses);
            return -1;
        }
    }

    /* Query each namespace, collect results */
    result_list results;
    result_list_init(&results);

    for (int i = 0; i < n_ns; i++) {
        query_into(db, nses[i].id, nses[i].name, qvec, dim,
                   query_text, opts, &results);
    }

    free(qvec);
    for (int i = 0; i < n_ns; i++) free(nses[i].name);
    free(nses);

    int n = print_results(&results, opts, 1);
    result_list_free(&results);
    return n;
}
