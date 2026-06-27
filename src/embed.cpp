#include "embed.h"

#include "llama.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

struct vecfile_embedder {
    struct llama_model   *model;
    struct llama_context *ctx;
    const struct llama_vocab *vocab;
    int dim;
};

static void silent_log(enum ggml_log_level level, const char *text, void *user_data) {
    (void)level; (void)text; (void)user_data;
}

vecfile_embedder *vecfile_embedder_create(const char *model_path) {
    llama_log_set(silent_log, nullptr);
    llama_backend_init();

    struct llama_model_params mparams = llama_model_default_params();
    mparams.use_mmap = true;

    struct llama_model *model = llama_model_load_from_file(model_path, mparams);
    if (!model) {
        fprintf(stderr, "vecfile: failed to load model: %s\n", model_path);
        return nullptr;
    }

    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 512;         /* bge-small context window */
    cparams.n_batch = 512;
    cparams.n_ubatch = 512;
    cparams.embeddings = true;   /* enable embedding output */
    cparams.n_threads = 4;

    struct llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        fprintf(stderr, "vecfile: failed to create context\n");
        llama_model_free(model);
        return nullptr;
    }

    auto *emb = (vecfile_embedder *)calloc(1, sizeof(vecfile_embedder));
    emb->model = model;
    emb->ctx = ctx;
    emb->vocab = llama_model_get_vocab(model);
    emb->dim = llama_model_n_embd(model);

    return emb;
}

int vecfile_embedder_dim(const vecfile_embedder *emb) {
    return emb ? emb->dim : 0;
}

/* L2-normalize in place */
static void normalize(float *vec, int dim) {
    float sum = 0.0f;
    for (int i = 0; i < dim; i++) sum += vec[i] * vec[i];
    float norm = sqrtf(sum);
    if (norm > 0.0f) {
        for (int i = 0; i < dim; i++) vec[i] /= norm;
    }
}

int vecfile_embed(vecfile_embedder *emb, const char *text, int text_len,
                  float *out, int max_dim) {
    if (!emb || !text || !out) return -1;
    if (max_dim < emb->dim) return -2;

    /* Tokenize */
    int n_max = text_len + 16;
    std::vector<llama_token> tokens(n_max);
    int n_tokens = llama_tokenize(emb->vocab, text, text_len,
                                  tokens.data(), n_max, true, true);
    if (n_tokens < 0) {
        /* Buffer too small — resize and retry */
        n_max = -n_tokens;
        tokens.resize(n_max);
        n_tokens = llama_tokenize(emb->vocab, text, text_len,
                                  tokens.data(), n_max, true, true);
        if (n_tokens < 0) return -3;
    }

    /* Truncate to context window */
    int n_ctx = (int)llama_n_ctx(emb->ctx);
    if (n_tokens > n_ctx) n_tokens = n_ctx;

    /* Build batch */
    struct llama_batch batch = llama_batch_init(n_tokens, 0, 1);
    for (int i = 0; i < n_tokens; i++) {
        batch.token[i]    = tokens[i];
        batch.pos[i]      = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i]   = (i == n_tokens - 1);  /* only need last for non-pooled */
    }
    batch.n_tokens = n_tokens;

    /* Clear memory (KV cache) and decode */
    llama_memory_clear(llama_get_memory(emb->ctx), true);
    int rc = llama_decode(emb->ctx, batch);
    llama_batch_free(batch);
    if (rc != 0) return -4;

    /* Extract embeddings */
    float *embeddings = nullptr;
    enum llama_pooling_type ptype = llama_pooling_type(emb->ctx);
    if (ptype == LLAMA_POOLING_TYPE_NONE) {
        embeddings = llama_get_embeddings_ith(emb->ctx, n_tokens - 1);
    } else {
        embeddings = llama_get_embeddings_seq(emb->ctx, 0);
    }
    if (!embeddings) return -5;

    memcpy(out, embeddings, emb->dim * sizeof(float));
    normalize(out, emb->dim);

    return emb->dim;
}

void vecfile_embedder_free(vecfile_embedder *emb) {
    if (!emb) return;
    if (emb->ctx) llama_free(emb->ctx);
    if (emb->model) llama_model_free(emb->model);
    free(emb);
    llama_backend_free();
}
