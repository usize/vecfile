#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"
#include "sqlite-vec.h"
#include "embed.h"
#include "schema.h"
#include "ingest.h"
#include "query.h"
#include "platform.h"

#define VECFILE_VERSION "0.1.0"
#define VECFILE_MODEL   "bge-small-en-v1.5"
#define VECFILE_DIM     384

static void usage(void) {
    fprintf(stderr,
        "vecfile v" VECFILE_VERSION " — single-file hybrid search engine\n\n"
        "Usage:\n"
        "  vecfile ns create  --db PATH --name NS [options]\n"
        "  vecfile ns list    --db PATH\n"
        "  vecfile ns info    --db PATH --name NS\n"
        "  vecfile ns delete  --db PATH --name NS\n"
        "\n"
        "  vecfile add    --db PATH --ns NS [--tag NAME] [--meta JSON]\n"
        "                 [--on-dup skip|replace]\n"
        "                 (\"text\" | - | --file F | file1 file2 ... | *.txt)\n"
        "  vecfile ls     --db PATH [--ns NS] [--tags]\n"
        "  vecfile delete --db PATH --ns NS (--id N | --path P | --all)\n"
        "  vecfile query  --db PATH (--ns NS | --all) [--limit N] [--pool N]\n"
        "                 [--semantic-only|--lexical-only] [--chunks]\n"
        "                 \"query text\"\n"
        "  vecfile get    --db PATH (--id N | --tag T | --chunk N [-C N])\n"
        "\n"
        "  vecfile model\n"
        "  vecfile --version\n"
    );
}

/* Find a flag value: --flag VALUE */
static const char *flag(int argc, char **argv, const char *name) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return NULL;
}

/* Check if a flag exists (no value) */
static int has_flag(int argc, char **argv, const char *name) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) return 1;
    }
    return 0;
}

/* Boolean flags (no value). Everything else with -- takes a value. */
static int is_bool_flag(const char *f) {
    return strcmp(f, "--chunks") == 0
        || strcmp(f, "--semantic-only") == 0
        || strcmp(f, "--lexical-only") == 0
        || strcmp(f, "--all") == 0
        || strcmp(f, "--tags") == 0
        || strcmp(f, "--version") == 0
        || strcmp(f, "-") == 0;
}

/* Get the last non-flag argument (positional).
   Skips values that belong to --flag VALUE pairs. */
static const char *positional(int argc, char **argv) {
    for (int i = argc - 1; i >= 1; i--) {
        if (argv[i][0] == '-') continue;
        /* Check if this arg is the value of a preceding value-flag */
        if (i > 1 && argv[i-1][0] == '-' && argv[i-1][1] == '-'
            && !is_bool_flag(argv[i-1])) continue;
        return argv[i];
    }
    return NULL;
}

static sqlite3 *open_db(const char *path) {
    sqlite3 *db;
    int rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "cannot open database: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    /* Enable foreign keys and WAL mode */
    sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA journal_mode = WAL", NULL, NULL, NULL);

    vecfile_schema_init(db);
    return db;
}

/* Read all of stdin into a malloc'd buffer */
static char *read_stdin(int *out_len) {
    int cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    int n;
    while ((n = (int)fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
    }
    *out_len = len;
    return buf;
}

/* Read a file into a malloc'd buffer */
static char *read_file(const char *path, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open file: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    *out_len = (int)sz;
    return buf;
}

/* ── Commands ────────────────────────────────────────────────── */

static int cmd_ns_create(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    const char *name = flag(argc, argv, "--name");
    if (!db_path || !name) {
        fprintf(stderr, "usage: vecfile ns create --db PATH --name NS\n");
        return 1;
    }

    const char *cs = flag(argc, argv, "--chunk-strategy");
    const char *cu = flag(argc, argv, "--chunk-unit");
    const char *csz = flag(argc, argv, "--chunk-size");
    const char *co = flag(argc, argv, "--chunk-overlap");

    int chunk_size = csz ? atoi(csz) : 512;
    int chunk_overlap = co ? atoi(co) : 64;

    sqlite3 *db = open_db(db_path);
    if (!db) return 1;

    int64_t id = vecfile_ns_create(db, name,
        VECFILE_MODEL, VECFILE_DIM,
        cs ? cs : "fixed", chunk_size, chunk_overlap,
        cu ? cu : "char");

    sqlite3_close(db);
    if (id < 0) return 1;

    printf("created namespace '%s' (id=%lld, dim=%d, chunk=%d/%d)\n",
           name, (long long)id, VECFILE_DIM, chunk_size, chunk_overlap);
    return 0;
}

static int cmd_ns_list(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    if (!db_path) {
        fprintf(stderr, "usage: vecfile ns list --db PATH\n");
        return 1;
    }
    sqlite3 *db = open_db(db_path);
    if (!db) return 1;
    vecfile_ns_list(db);
    sqlite3_close(db);
    return 0;
}

static int cmd_ns_info(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    const char *name = flag(argc, argv, "--name");
    if (!db_path || !name) {
        fprintf(stderr, "usage: vecfile ns info --db PATH --name NS\n");
        return 1;
    }
    sqlite3 *db = open_db(db_path);
    if (!db) return 1;
    int rc = vecfile_ns_info(db, name);
    sqlite3_close(db);
    return rc < 0 ? 1 : 0;
}

static int cmd_ns_delete(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    const char *name = flag(argc, argv, "--name");
    if (!db_path || !name) {
        fprintf(stderr, "usage: vecfile ns delete --db PATH --name NS\n");
        return 1;
    }
    sqlite3 *db = open_db(db_path);
    if (!db) return 1;
    int rc = vecfile_ns_delete(db, name);
    sqlite3_close(db);
    if (rc == 0) printf("deleted namespace '%s'\n", name);
    return rc < 0 ? 1 : 0;
}

/* Collect bare file paths from argv (args that aren't flags, flag values,
   or the command name). These come from shell wildcard expansion. */
static int collect_files(int argc, char **argv, const char ***out) {
    /* First pass: count */
    int count = 0;
    for (int i = 2; i < argc; i++) {  /* skip argv[0] and "add" */
        if (argv[i][0] == '-') {
            /* Skip value-flag and its value */
            if (argv[i][1] == '-' && !is_bool_flag(argv[i])
                && i + 1 < argc && argv[i+1][0] != '-')
                i++;
            continue;
        }
        /* Check if this is a value for a preceding value-flag */
        if (i > 1 && argv[i-1][0] == '-' && argv[i-1][1] == '-'
            && !is_bool_flag(argv[i-1])) continue;
        count++;
    }

    if (count == 0) { *out = NULL; return 0; }

    const char **files = (const char **)malloc(count * sizeof(char *));
    int idx = 0;
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == '-' && !is_bool_flag(argv[i])
                && i + 1 < argc && argv[i+1][0] != '-')
                i++;
            continue;
        }
        if (i > 1 && argv[i-1][0] == '-' && argv[i-1][1] == '-'
            && !is_bool_flag(argv[i-1])) continue;
        files[idx++] = argv[i];
    }

    *out = files;
    return count;
}

static int cmd_add(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    const char *ns_name = flag(argc, argv, "--ns");
    if (!db_path || !ns_name) {
        fprintf(stderr, "usage: vecfile add --db PATH --ns NS"
                " [--tag T] [--meta JSON] [--on-dup skip|replace]\n"
                "       (\"text\" | - | --file F | file1 file2 ... | *.txt)\n");
        return 1;
    }

    const char *single_file = flag(argc, argv, "--file");
    const char *tag = flag(argc, argv, "--tag");
    const char *meta = flag(argc, argv, "--meta");
    const char *dup_str = flag(argc, argv, "--on-dup");
    int on_dup = (dup_str && strcmp(dup_str, "replace") == 0) ? 1 : 0;

    /* Collect bare positional args (shell-expanded wildcards or literal text) */
    const char **bare_args = NULL;
    int n_bare = collect_files(argc, argv, &bare_args);

    /* Determine mode: multi-file, single --file, stdin, or literal text */
    int is_stdin = has_flag(argc, argv, "-");
    int is_multi_file = 0;

    /* Heuristic: if there are multiple bare args, or a bare arg looks like
       a file path (contains / or .), treat them as files */
    if (n_bare > 1) {
        is_multi_file = 1;
    } else if (n_bare == 1 && !single_file && !is_stdin) {
        /* Single bare arg: is it a file path or literal text? */
        FILE *test = fopen(bare_args[0], "r");
        if (test) {
            fclose(test);
            is_multi_file = 1;  /* it's an existing file */
        }
        /* otherwise treat as literal text */
    }

    /* Open DB and model once for all files */
    sqlite3_auto_extension((void (*)(void))sqlite3_vec_init);
    sqlite3 *db = open_db(db_path);
    if (!db) { free(bare_args); return 1; }

    int64_t ns_id = vecfile_ns_lookup(db, ns_name);
    if (ns_id < 0) {
        fprintf(stderr, "namespace '%s' not found\n", ns_name);
        sqlite3_close(db); free(bare_args); return 1;
    }

    int ns_dim = vecfile_ns_dim(db, ns_id);
    if (ns_dim != VECFILE_DIM) {
        fprintf(stderr, "namespace dim %d doesn't match binary model dim %d\n",
                ns_dim, VECFILE_DIM);
        sqlite3_close(db); free(bare_args); return 1;
    }

    vecfile_embedder *emb = vecfile_embedder_create(NULL);
    if (!emb) {
        fprintf(stderr, "failed to load embedding model\n");
        sqlite3_close(db); free(bare_args); return 1;
    }

    int added = 0, errors = 0;

    if (is_multi_file) {
        /* Multi-file mode: process each bare arg as a file */
        for (int i = 0; i < n_bare; i++) {
            int content_len = 0;
            char *content = read_file(bare_args[i], &content_len);
            if (!content) { errors++; continue; }

            int64_t fid = vecfile_add(db, ns_id, emb,
                content, content_len, bare_args[i], meta, 0, on_dup);
            free(content);

            if (fid < 0) {
                fprintf(stderr, "error: %s\n", bare_args[i]);
                errors++;
            } else {
                /* Check if it was a dedup skip (same id returned, no new insert) */
                added++;
                printf("  %s -> file_id=%lld\n", bare_args[i], (long long)fid);
            }
        }
        printf("done: %d added, %d errors\n", added, errors);

    } else if (single_file) {
        /* Single --file */
        int content_len = 0;
        char *content = read_file(single_file, &content_len);
        if (!content) { errors = 1; goto cleanup; }

        const char *prov = tag ? tag : single_file;
        int64_t fid = vecfile_add(db, ns_id, emb,
            content, content_len, prov, meta, 0, on_dup);
        free(content);

        if (fid < 0) { fprintf(stderr, "add failed\n"); errors = 1; }
        else printf("added file_id=%lld\n", (long long)fid);

    } else if (is_stdin) {
        /* Stdin */
        int content_len = 0;
        char *content = read_stdin(&content_len);
        if (!content || content_len == 0) {
            fprintf(stderr, "no content on stdin\n");
            free(content); errors = 1; goto cleanup;
        }

        int64_t fid = vecfile_add(db, ns_id, emb,
            content, content_len, tag, meta, 0, on_dup);
        free(content);

        if (fid < 0) { fprintf(stderr, "add failed\n"); errors = 1; }
        else printf("added file_id=%lld\n", (long long)fid);

    } else if (n_bare == 1) {
        /* Literal text (single bare arg that isn't a file) */
        int content_len = (int)strlen(bare_args[0]);
        int64_t fid = vecfile_add(db, ns_id, emb,
            bare_args[0], content_len, tag, meta, 0, on_dup);

        if (fid < 0) { fprintf(stderr, "add failed\n"); errors = 1; }
        else printf("added file_id=%lld\n", (long long)fid);

    } else {
        fprintf(stderr, "no content provided\n");
        errors = 1;
    }

cleanup:
    vecfile_embedder_free(emb);
    sqlite3_close(db);
    free(bare_args);
    return errors > 0 ? 1 : 0;
}

static int cmd_delete(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    const char *ns_name = flag(argc, argv, "--ns");
    if (!db_path || !ns_name) {
        fprintf(stderr, "usage: vecfile delete --db PATH --ns NS (--id N | --path P | --all)\n");
        return 1;
    }

    sqlite3_auto_extension((void (*)(void))sqlite3_vec_init);
    sqlite3 *db = open_db(db_path);
    if (!db) return 1;

    int64_t ns_id = vecfile_ns_lookup(db, ns_name);
    if (ns_id < 0) {
        fprintf(stderr, "namespace '%s' not found\n", ns_name);
        sqlite3_close(db); return 1;
    }

    int rc;
    const char *id_str = flag(argc, argv, "--id");
    const char *path = flag(argc, argv, "--path");

    if (id_str) {
        rc = vecfile_delete_by_id(db, ns_id, atoll(id_str));
    } else if (path) {
        rc = vecfile_delete_by_path(db, ns_id, path);
    } else if (has_flag(argc, argv, "--all")) {
        rc = vecfile_delete_all(db, ns_id);
    } else {
        fprintf(stderr, "specify --id, --path, or --all\n");
        sqlite3_close(db); return 1;
    }

    sqlite3_close(db);
    if (rc == 0) printf("deleted\n");
    return rc < 0 ? 1 : 0;
}

static int cmd_query(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    const char *ns_name = flag(argc, argv, "--ns");
    int query_all = has_flag(argc, argv, "--all");

    if (!db_path || (!ns_name && !query_all)) {
        fprintf(stderr, "usage: vecfile query --db PATH (--ns NS | --all) \"query text\"\n");
        return 1;
    }

    const char *query_text = positional(argc, argv);
    if (!query_text || strcmp(query_text, "query") == 0) {
        fprintf(stderr, "no query text provided\n");
        return 1;
    }

    sqlite3_auto_extension((void (*)(void))sqlite3_vec_init);
    sqlite3 *db = open_db(db_path);
    if (!db) return 1;

    vecfile_query_opts opts;
    vecfile_query_opts_default(&opts);

    const char *lim = flag(argc, argv, "--limit");
    if (lim) opts.limit = atoi(lim);
    const char *pool = flag(argc, argv, "--pool");
    if (pool) opts.pool = atoi(pool);
    if (has_flag(argc, argv, "--semantic-only")) opts.mode = VECFILE_QUERY_SEMANTIC;
    if (has_flag(argc, argv, "--lexical-only")) opts.mode = VECFILE_QUERY_LEXICAL;
    if (has_flag(argc, argv, "--chunks")) opts.return_chunks = 1;

    vecfile_embedder *emb = NULL;
    if (opts.mode != VECFILE_QUERY_LEXICAL) {
        emb = vecfile_embedder_create(NULL);
        if (!emb) {
            fprintf(stderr, "failed to load embedding model\n");
            sqlite3_close(db); return 1;
        }
    }

    int n;
    if (query_all) {
        n = vecfile_query_all(db, emb, query_text, &opts);
    } else {
        int64_t ns_id = vecfile_ns_lookup(db, ns_name);
        if (ns_id < 0) {
            fprintf(stderr, "namespace '%s' not found\n", ns_name);
            if (emb) vecfile_embedder_free(emb);
            sqlite3_close(db); return 1;
        }
        n = vecfile_query(db, ns_id, emb, query_text, &opts);
    }

    if (emb) vecfile_embedder_free(emb);
    sqlite3_close(db);

    if (n < 0) return 1;
    if (n == 0) printf("(no results)\n");
    return 0;
}

static int cmd_ls(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    if (!db_path) {
        fprintf(stderr, "usage: vecfile ls --db PATH [--ns NS]\n");
        return 1;
    }

    const char *ns_name = flag(argc, argv, "--ns");
    int tags_only = has_flag(argc, argv, "--tags");

    sqlite3 *db = open_db(db_path);
    if (!db) return 1;

    sqlite3_stmt *stmt;
    int rc;

    if (ns_name) {
        /* List files in a specific namespace */
        int64_t ns_id = vecfile_ns_lookup(db, ns_name);
        if (ns_id < 0) {
            fprintf(stderr, "namespace '%s' not found\n", ns_name);
            sqlite3_close(db); return 1;
        }

        if (tags_only) {
            rc = sqlite3_prepare_v2(db,
                "SELECT path FROM files WHERE namespace_id = ?"
                " AND path IS NOT NULL ORDER BY path",
                -1, &stmt, NULL);
            sqlite3_bind_int64(stmt, 1, ns_id);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                printf("%s\n", sqlite3_column_text(stmt, 0));
            }
        } else {
            rc = sqlite3_prepare_v2(db,
                "SELECT id, path, bytes, chunk_count, added_at FROM files"
                " WHERE namespace_id = ? ORDER BY id",
                -1, &stmt, NULL);
            sqlite3_bind_int64(stmt, 1, ns_id);
            int count = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *path = (const char *)sqlite3_column_text(stmt, 1);
                int bytes = sqlite3_column_int(stmt, 2);
                int chunks = sqlite3_column_int(stmt, 3);
                printf("  %-6lld  %-40s  %6d bytes  %3d chunks\n",
                    (long long)sqlite3_column_int64(stmt, 0),
                    path ? path : "(stdin)",
                    bytes, chunks);
                count++;
            }
            if (count == 0) printf("(empty)\n");
        }
        sqlite3_finalize(stmt);

    } else {
        /* List files across all namespaces */
        if (tags_only) {
            rc = sqlite3_prepare_v2(db,
                "SELECT n.name, f.path FROM files f"
                " JOIN namespaces n ON n.id = f.namespace_id"
                " WHERE f.path IS NOT NULL"
                " ORDER BY n.name, f.path",
                -1, &stmt, NULL);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                printf("%s\t%s\n",
                    sqlite3_column_text(stmt, 0),
                    sqlite3_column_text(stmt, 1));
            }
        } else {
            rc = sqlite3_prepare_v2(db,
                "SELECT f.id, n.name, f.path, f.bytes, f.chunk_count"
                " FROM files f"
                " JOIN namespaces n ON n.id = f.namespace_id"
                " ORDER BY n.name, f.id",
                -1, &stmt, NULL);
            int count = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *ns = (const char *)sqlite3_column_text(stmt, 1);
                const char *path = (const char *)sqlite3_column_text(stmt, 2);
                int bytes = sqlite3_column_int(stmt, 3);
                int chunks = sqlite3_column_int(stmt, 4);
                printf("  %-6lld  %-12s  %-40s  %6d bytes  %3d chunks\n",
                    (long long)sqlite3_column_int64(stmt, 0),
                    ns,
                    path ? path : "(stdin)",
                    bytes, chunks);
                count++;
            }
            if (count == 0) printf("(empty)\n");
        }
        sqlite3_finalize(stmt);
        (void)rc;
    }

    sqlite3_close(db);
    return 0;
}

static int cmd_get(int argc, char **argv) {
    const char *db_path = flag(argc, argv, "--db");
    if (!db_path) {
        fprintf(stderr, "usage: vecfile get --db PATH (--id N | --tag T | --chunk N [-C N])\n");
        return 1;
    }

    const char *id_str = flag(argc, argv, "--id");
    const char *tag = flag(argc, argv, "--tag");
    const char *chunk_str = flag(argc, argv, "--chunk");
    const char *ctx_str = flag(argc, argv, "-C");
    int context = ctx_str ? atoi(ctx_str) : 0;

    if (!id_str && !tag && !chunk_str) {
        fprintf(stderr, "usage: vecfile get --db PATH (--id N | --tag T | --chunk N [-C N])\n");
        return 1;
    }

    sqlite3 *db = open_db(db_path);
    if (!db) return 1;

    sqlite3_stmt *stmt;

    if (chunk_str) {
        /* Get chunk by id, with optional surrounding context */
        int64_t chunk_id = atoll(chunk_str);

        /* First, find the chunk's file_id and ordinal */
        sqlite3_prepare_v2(db,
            "SELECT file_id, ordinal FROM chunks WHERE id = ?",
            -1, &stmt, NULL);
        sqlite3_bind_int64(stmt, 1, chunk_id);

        if (sqlite3_step(stmt) != SQLITE_ROW) {
            fprintf(stderr, "chunk not found\n");
            sqlite3_finalize(stmt); sqlite3_close(db); return 1;
        }
        int64_t file_id = sqlite3_column_int64(stmt, 0);
        int ordinal = sqlite3_column_int(stmt, 1);
        sqlite3_finalize(stmt);

        /* Fetch the chunk and its neighbors */
        int ord_min = ordinal - context;
        if (ord_min < 0) ord_min = 0;
        int ord_max = ordinal + context;

        sqlite3_prepare_v2(db,
            "SELECT id, ordinal, content FROM chunks"
            " WHERE file_id = ? AND ordinal BETWEEN ? AND ?"
            " ORDER BY ordinal",
            -1, &stmt, NULL);
        sqlite3_bind_int64(stmt, 1, file_id);
        sqlite3_bind_int(stmt, 2, ord_min);
        sqlite3_bind_int(stmt, 3, ord_max);

        int first = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t cid = sqlite3_column_int64(stmt, 0);
            int ord = sqlite3_column_int(stmt, 1);
            const char *text = (const char *)sqlite3_column_text(stmt, 2);
            if (context > 0) {
                /* With context, show chunk boundaries */
                if (!first) printf("\n");
                printf("--- chunk %lld (ordinal %d)%s ---\n",
                       (long long)cid, ord,
                       (cid == chunk_id) ? " [match]" : "");
            }
            printf("%s", text);
            if (context > 0) printf("\n");
            first = 0;
        }
        sqlite3_finalize(stmt);

    } else {
        /* Get file by id or tag */
        if (id_str) {
            sqlite3_prepare_v2(db,
                "SELECT content FROM files WHERE id = ?", -1, &stmt, NULL);
            sqlite3_bind_int64(stmt, 1, atoll(id_str));
        } else {
            sqlite3_prepare_v2(db,
                "SELECT content FROM files WHERE path = ?", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_TRANSIENT);
        }

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *content = (const char *)sqlite3_column_text(stmt, 0);
            printf("%s", content);
        } else {
            fprintf(stderr, "not found\n");
            sqlite3_finalize(stmt); sqlite3_close(db); return 1;
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return 0;
}

static int cmd_model(void) {
    printf("model: " VECFILE_MODEL "\n");
    printf("dim:   %d\n", VECFILE_DIM);
    printf("quant: Q8_0\n");
    printf("file:  " VECFILE_BUNDLED_MODEL_PATH "\n");
    return 0;
}

/* ── Main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(); return 1; }

    /* Register sqlite-vec globally */
    sqlite3_auto_extension((void (*)(void))sqlite3_vec_init);

    const char *cmd = argv[1];

    if (strcmp(cmd, "--version") == 0) {
        printf("vecfile v" VECFILE_VERSION " (SQLite %s, sqlite-vec %s)\n",
               sqlite3_libversion(), SQLITE_VEC_VERSION);
        return 0;
    }
    if (strcmp(cmd, "model") == 0) return cmd_model();

    /* Namespace subcommands: vecfile ns <sub> */
    if (strcmp(cmd, "ns") == 0 && argc >= 3) {
        const char *sub = argv[2];
        if (strcmp(sub, "create") == 0) return cmd_ns_create(argc, argv);
        if (strcmp(sub, "list") == 0)   return cmd_ns_list(argc, argv);
        if (strcmp(sub, "info") == 0)   return cmd_ns_info(argc, argv);
        if (strcmp(sub, "delete") == 0) return cmd_ns_delete(argc, argv);
        fprintf(stderr, "unknown ns subcommand: %s\n", sub);
        return 1;
    }

    if (strcmp(cmd, "add") == 0)    return cmd_add(argc, argv);
    if (strcmp(cmd, "ls") == 0)     return cmd_ls(argc, argv);
    if (strcmp(cmd, "delete") == 0) return cmd_delete(argc, argv);
    if (strcmp(cmd, "query") == 0)  return cmd_query(argc, argv);
    if (strcmp(cmd, "get") == 0)    return cmd_get(argc, argv);

    fprintf(stderr, "unknown command: %s\n", cmd);
    usage();
    return 1;
}
