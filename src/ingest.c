#include "ingest.h"
#include "schema.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── SHA-256 (minimal, no deps) ─────────────────────────────── */

static void sha256_hex(const char *data, int len, char out[65]) {
    /* Use SQLite's built-in hex + hash via a query. Simple and correct. */
    /* We'll compute it inline instead for zero-dep. */
    /* For now, use a simple hash. We'll swap to real SHA-256. */
    /* Actually, let's use the standard approach with a lookup table. */

    /* Minimal SHA-256 implementation */
    unsigned int h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;
    unsigned int h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;

    static const unsigned int k[64] = {
        0x428a2f98,0x71374491,0xb5c0cf66,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    /* Pre-processing: pad message */
    unsigned long long bit_len = (unsigned long long)len * 8;
    int padded_len = ((len + 8) / 64 + 1) * 64;
    unsigned char *msg = (unsigned char *)calloc(1, padded_len);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    for (int i = 0; i < 8; i++)
        msg[padded_len - 1 - i] = (unsigned char)(bit_len >> (i * 8));

    /* Process each 64-byte block */
    for (int offset = 0; offset < padded_len; offset += 64) {
        unsigned int w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ((unsigned int)msg[offset+i*4] << 24) |
                    ((unsigned int)msg[offset+i*4+1] << 16) |
                    ((unsigned int)msg[offset+i*4+2] << 8) |
                    ((unsigned int)msg[offset+i*4+3]);
        }
        for (int i = 16; i < 64; i++) {
            unsigned int s0 = ((w[i-15]>>7)|(w[i-15]<<25)) ^ ((w[i-15]>>18)|(w[i-15]<<14)) ^ (w[i-15]>>3);
            unsigned int s1 = ((w[i-2]>>17)|(w[i-2]<<15)) ^ ((w[i-2]>>19)|(w[i-2]<<13)) ^ (w[i-2]>>10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }

        unsigned int a=h0, b=h1, c=h2, d=h3, e=h4, f=h5, g=h6, h=h7;
        for (int i = 0; i < 64; i++) {
            unsigned int S1 = ((e>>6)|(e<<26)) ^ ((e>>11)|(e<<21)) ^ ((e>>25)|(e<<7));
            unsigned int ch = (e & f) ^ ((~e) & g);
            unsigned int temp1 = h + S1 + ch + k[i] + w[i];
            unsigned int S0 = ((a>>2)|(a<<30)) ^ ((a>>13)|(a<<19)) ^ ((a>>22)|(a<<10));
            unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            unsigned int temp2 = S0 + maj;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e; h5+=f; h6+=g; h7+=h;
    }
    free(msg);

    snprintf(out, 65, "%08x%08x%08x%08x%08x%08x%08x%08x",
             h0, h1, h2, h3, h4, h5, h6, h7);
}

/* ── Fixed-window chunking ──────────────────────────────────── */

typedef struct {
    const char *text;
    int start;
    int end;
} chunk_t;

/* Simple character-based fixed-window chunking.
   Token-based chunking would use the model's tokenizer;
   char-based is the v1 default and avoids a C++ dependency here. */
static int chunk_fixed(const char *content, int content_len,
                       int chunk_size, int chunk_overlap,
                       chunk_t **out_chunks) {
    if (content_len <= 0) return 0;

    int step = chunk_size - chunk_overlap;
    if (step <= 0) step = chunk_size;

    /* Count chunks */
    int n = 0;
    for (int off = 0; off < content_len; off += step) n++;

    chunk_t *chunks = (chunk_t *)calloc(n, sizeof(chunk_t));
    int count = 0;
    for (int off = 0; off < content_len; off += step) {
        int end = off + chunk_size;
        if (end > content_len) end = content_len;
        chunks[count].text = content + off;
        chunks[count].start = off;
        chunks[count].end = end;
        count++;
        if (end == content_len) break;
    }

    *out_chunks = chunks;
    return count;
}

/* ── Add ────────────────────────────────────────────────────── */

int64_t vecfile_add(sqlite3 *db, int64_t ns_id, vecfile_embedder *emb,
                    const char *content, int content_len,
                    const char *path, const char *meta,
                    int64_t doc_date, int on_dup) {
    /* Compute sha256 of content */
    char sha[65];
    sha256_hex(content, content_len, sha);

    /* Dedup check */
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT id FROM files WHERE namespace_id = ? AND sha256 = ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int64(stmt, 1, ns_id);
    sqlite3_bind_text(stmt, 2, sha, -1, SQLITE_STATIC);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    int64_t existing_id = exists ? sqlite3_column_int64(stmt, 0) : -1;
    sqlite3_finalize(stmt);

    if (exists && on_dup == 0) {
        /* skip */
        return existing_id;
    }
    if (exists && on_dup == 1) {
        /* replace — delete old, then re-add */
        vecfile_delete_by_id(db, ns_id, existing_id);
    }

    /* Get namespace config for chunking */
    rc = sqlite3_prepare_v2(db,
        "SELECT chunk_strategy, chunk_size, chunk_overlap, chunk_unit, dim"
        " FROM namespaces WHERE id = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int64(stmt, 1, ns_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        fprintf(stderr, "namespace %lld not found\n", (long long)ns_id);
        return -1;
    }

    int chunk_size = sqlite3_column_int(stmt, 1);
    int chunk_overlap = sqlite3_column_int(stmt, 2);
    int dim = sqlite3_column_int(stmt, 4);
    sqlite3_finalize(stmt);

    /* Begin transaction */
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);

    /* Insert file row */
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO files(namespace_id, path, content, meta, doc_date,"
        " sha256, bytes, chunk_count, added_at)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, 0, ?)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, ns_id);
    if (path) sqlite3_bind_text(stmt, 2, path, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, content, content_len, SQLITE_TRANSIENT);
    if (meta) sqlite3_bind_text(stmt, 4, meta, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 4);
    if (doc_date > 0) sqlite3_bind_int64(stmt, 5, doc_date);
    else sqlite3_bind_null(stmt, 5);
    sqlite3_bind_text(stmt, 6, sha, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, content_len);
    sqlite3_bind_int64(stmt, 8, vecfile_now_unix());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "file insert: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }
    int64_t file_id = sqlite3_last_insert_rowid(db);

    /* Chunk the content */
    chunk_t *chunks = NULL;
    int n_chunks = chunk_fixed(content, content_len, chunk_size, chunk_overlap,
                               &chunks);

    /* Prepare statements for chunk insert, FTS insert, vec insert */
    sqlite3_stmt *chunk_stmt, *fts_stmt, *vec_stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO chunks(namespace_id, file_id, ordinal, content, sha256,"
        " start_off, end_off) VALUES(?, ?, ?, ?, ?, ?, ?)",
        -1, &chunk_stmt, NULL);

    char fts_sql[128], vec_sql[128];
    snprintf(fts_sql, sizeof(fts_sql),
        "INSERT INTO chunks_fts_%lld(rowid, content) VALUES(?, ?)",
        (long long)ns_id);
    snprintf(vec_sql, sizeof(vec_sql),
        "INSERT INTO chunks_vec_%lld(rowid, embedding, doc_date) VALUES(?, ?, ?)",
        (long long)ns_id);
    sqlite3_prepare_v2(db, fts_sql, -1, &fts_stmt, NULL);
    sqlite3_prepare_v2(db, vec_sql, -1, &vec_stmt, NULL);

    float *vec_buf = (float *)malloc(dim * sizeof(float));
    int ok = 1;

    for (int i = 0; i < n_chunks && ok; i++) {
        int clen = chunks[i].end - chunks[i].start;
        char chunk_sha[65];
        sha256_hex(chunks[i].text, clen, chunk_sha);

        /* Insert chunk row */
        sqlite3_reset(chunk_stmt);
        sqlite3_bind_int64(chunk_stmt, 1, ns_id);
        sqlite3_bind_int64(chunk_stmt, 2, file_id);
        sqlite3_bind_int(chunk_stmt, 3, i);
        sqlite3_bind_text(chunk_stmt, 4, chunks[i].text, clen, SQLITE_TRANSIENT);
        sqlite3_bind_text(chunk_stmt, 5, chunk_sha, -1, SQLITE_STATIC);
        sqlite3_bind_int(chunk_stmt, 6, chunks[i].start);
        sqlite3_bind_int(chunk_stmt, 7, chunks[i].end);
        if (sqlite3_step(chunk_stmt) != SQLITE_DONE) { ok = 0; break; }

        int64_t chunk_id = sqlite3_last_insert_rowid(db);

        /* Insert into FTS */
        sqlite3_reset(fts_stmt);
        sqlite3_bind_int64(fts_stmt, 1, chunk_id);
        sqlite3_bind_text(fts_stmt, 2, chunks[i].text, clen, SQLITE_TRANSIENT);
        if (sqlite3_step(fts_stmt) != SQLITE_DONE) { ok = 0; break; }

        /* Embed and insert into vec */
        int edim = vecfile_embed(emb, chunks[i].text, clen, vec_buf, dim);
        if (edim < 0) {
            fprintf(stderr, "embed failed for chunk %d: %d\n", i, edim);
            ok = 0; break;
        }

        sqlite3_reset(vec_stmt);
        sqlite3_bind_int64(vec_stmt, 1, chunk_id);
        sqlite3_bind_blob(vec_stmt, 2, vec_buf, dim * sizeof(float), SQLITE_TRANSIENT);
        if (doc_date > 0) sqlite3_bind_int64(vec_stmt, 3, doc_date);
        else sqlite3_bind_null(vec_stmt, 3);
        if (sqlite3_step(vec_stmt) != SQLITE_DONE) {
            fprintf(stderr, "vec insert: %s\n", sqlite3_errmsg(db));
            ok = 0; break;
        }
    }

    sqlite3_finalize(chunk_stmt);
    sqlite3_finalize(fts_stmt);
    sqlite3_finalize(vec_stmt);
    free(vec_buf);
    free(chunks);

    if (!ok) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }

    /* Update chunk count */
    rc = sqlite3_prepare_v2(db,
        "UPDATE files SET chunk_count = ? WHERE id = ?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, n_chunks);
        sqlite3_bind_int64(stmt, 2, file_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    return file_id;
}

/* ── Delete ─────────────────────────────────────────────────── */

static int delete_file_chunks(sqlite3 *db, int64_t ns_id, int64_t file_id) {
    /* Delete from per-namespace FTS and vec tables first */
    char sql[256];
    snprintf(sql, sizeof(sql),
        "DELETE FROM chunks_fts_%lld WHERE rowid IN"
        " (SELECT id FROM chunks WHERE file_id = %lld)",
        (long long)ns_id, (long long)file_id);
    sqlite3_exec(db, sql, NULL, NULL, NULL);

    snprintf(sql, sizeof(sql),
        "DELETE FROM chunks_vec_%lld WHERE rowid IN"
        " (SELECT id FROM chunks WHERE file_id = %lld)",
        (long long)ns_id, (long long)file_id);
    sqlite3_exec(db, sql, NULL, NULL, NULL);

    /* Delete chunks and file (CASCADE would handle chunks, but we already
       cleaned the virtual tables explicitly) */
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "DELETE FROM chunks WHERE file_id = ?", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "DELETE FROM files WHERE id = ?", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return 0;
}

int vecfile_delete_by_id(sqlite3 *db, int64_t ns_id, int64_t file_id) {
    return delete_file_chunks(db, ns_id, file_id);
}

int vecfile_delete_by_path(sqlite3 *db, int64_t ns_id, const char *path) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT id FROM files WHERE namespace_id = ? AND path = ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int64(stmt, 1, ns_id);
    sqlite3_bind_text(stmt, 2, path, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t file_id = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);

    return delete_file_chunks(db, ns_id, file_id);
}

int vecfile_delete_all(sqlite3 *db, int64_t ns_id) {
    char sql[128];

    /* Clear per-namespace virtual tables */
    snprintf(sql, sizeof(sql),
        "DELETE FROM chunks_fts_%lld", (long long)ns_id);
    sqlite3_exec(db, sql, NULL, NULL, NULL);

    snprintf(sql, sizeof(sql),
        "DELETE FROM chunks_vec_%lld", (long long)ns_id);
    sqlite3_exec(db, sql, NULL, NULL, NULL);

    /* Delete all chunks and files in the namespace */
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
        "DELETE FROM chunks WHERE namespace_id = ?", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, ns_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db,
        "DELETE FROM files WHERE namespace_id = ?", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, ns_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return 0;
}
