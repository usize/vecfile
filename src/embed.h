#ifndef VECFILE_EMBED_H
#define VECFILE_EMBED_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vecfile_embedder vecfile_embedder;

/* Load model from file path. Returns NULL on failure. */
vecfile_embedder *vecfile_embedder_create(const char *model_path);

/* Get embedding dimension. */
int vecfile_embedder_dim(const vecfile_embedder *emb);

/* Compute embedding for text. Writes dim floats to out.
   Returns dim on success, negative on error. */
int vecfile_embed(vecfile_embedder *emb, const char *text, int text_len,
                  float *out, int max_dim);

/* Free all resources. */
void vecfile_embedder_free(vecfile_embedder *emb);

#ifdef __cplusplus
}
#endif

#endif /* VECFILE_EMBED_H */
