#include <stdio.h>
#include <string.h>
#include "sqlite3.h"
#include "sqlite-vec.h"
#include "embed.h"

static int print_row(void *unused, int ncols, char **vals, char **names) {
    (void)unused;
    for (int i = 0; i < ncols; i++) {
        printf("  %s = %s\n", names[i], vals[i] ? vals[i] : "(null)");
    }
    printf("\n");
    return 0;
}

static void run_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, print_row, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err);
        sqlite3_free(err);
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Register sqlite-vec as an auto extension before opening any DB */
    sqlite3_auto_extension((void (*)(void))sqlite3_vec_init);

    printf("vecfile v0.0.1 (SQLite %s, sqlite-vec %s)\n\n",
           sqlite3_libversion(), SQLITE_VEC_VERSION);

    sqlite3 *db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    /* Phase 1 check: FTS5 works */
    printf("--- FTS5 query: 'semantic search' ---\n");
    run_sql(db,
        "CREATE VIRTUAL TABLE docs USING fts5(title, body);"
    );
    run_sql(db,
        "INSERT INTO docs VALUES"
        "  ('Memory Systems', 'Embedding vectors enable semantic recall over a personal corpus.'),"
        "  ('SQLite Everywhere', 'SQLite is the most deployed database engine in the world.'),"
        "  ('Search Engines', 'BM25 is a lexical ranking function used in full-text search.'),"
        "  ('Neural Networks', 'Transformers use attention mechanisms for sequence modeling.');"
    );
    run_sql(db,
        "SELECT title, rank FROM docs WHERE docs MATCH 'semantic OR search' ORDER BY rank LIMIT 5;"
    );

    /* Phase 2 check: vec0 works */
    printf("--- vec0: KNN query over 4 vectors (dim=4) ---\n");
    run_sql(db,
        "CREATE VIRTUAL TABLE test_vec USING vec0(embedding float[4]);"
    );
    run_sql(db,
        "INSERT INTO test_vec(rowid, embedding) VALUES"
        "  (1, X'0000803f000000000000000000000000'),"   /* [1,0,0,0] */
        "  (2, X'000000000000803f0000000000000000'),"   /* [0,1,0,0] */
        "  (3, X'00000000000000000000803f00000000'),"   /* [0,0,1,0] */
        "  (4, X'0000000000000000000000000000803f');"    /* [0,0,0,1] */
    );
    run_sql(db,
        "SELECT rowid, distance FROM test_vec"
        "  WHERE embedding MATCH X'0000803f000000000000000000000000'"
        "  AND k = 4"
        "  ORDER BY distance;"
    );

    /* Phase 3 check: embedding model */
    printf("--- Embedding test ---\n");
    const char *model_path = "models/bge-small-en-v1.5-q8_0.gguf";
    vecfile_embedder *emb = vecfile_embedder_create(model_path);
    if (!emb) {
        fprintf(stderr, "Failed to load model from %s\n", model_path);
        sqlite3_close(db);
        return 1;
    }
    printf("Model loaded: dim=%d\n", vecfile_embedder_dim(emb));

    const char *test_text = "What is semantic search?";
    float vec[384];
    int dim = vecfile_embed(emb, test_text, (int)strlen(test_text), vec, 384);
    if (dim > 0) {
        printf("Embedded \"%s\" -> [%.4f, %.4f, %.4f, ...] (dim=%d)\n",
               test_text, vec[0], vec[1], vec[2], dim);
    } else {
        fprintf(stderr, "Embedding failed: %d\n", dim);
    }

    vecfile_embedder_free(emb);
    sqlite3_close(db);
    return 0;
}
