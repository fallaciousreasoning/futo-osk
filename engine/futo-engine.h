/*
 * C API over FUTO Keyboard's prediction engine: the AOSP-derived binary
 * dictionary (typing correction and gesture decoding) and FUTO's
 * transformer language model.
 *
 * An engine is not thread safe; use it from one thread at a time.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define FUTO_MAX_WORD_BYTES 192

typedef struct FutoEngine FutoEngine;

/* A key that produces a character, in keyboard pixel coordinates. */
typedef struct {
    int code; /* Unicode code point; letters lowercase */
    int x, y, width, height;
} FutoKey;

typedef struct {
    char word[FUTO_MAX_WORD_BYTES];
    int score;
} FutoSuggestion;

typedef struct {
    int n;
    FutoSuggestion items[16];
    /* Index into items of the word to replace the typed word with when
     * a separator is typed, or -1 to keep what was typed. */
    int autocorrect;
} FutoSuggestions;

/* lm_path may be NULL to run with the dictionary only. */
FutoEngine *futo_engine_new(const char *dict_path, const char *lm_path, const char *locale);
void futo_engine_free(FutoEngine *engine);
int futo_engine_has_lm(const FutoEngine *engine);

/* Must be called before suggesting, and again whenever key geometry changes. */
void futo_engine_set_layout(FutoEngine *engine, int width, int height, const FutoKey *keys,
                            int n_keys);

/*
 * Suggestions while tapping. context is the text before the word being
 * typed; partial is that word, with xs/ys the tap position of each of its
 * characters (-1 when unknown). An empty partial predicts the next word.
 */
void futo_engine_suggest(FutoEngine *engine, const char *context, const char *partial,
                         const int *xs, const int *ys, int n_taps, FutoSuggestions *out);

/* Decodes a swipe. times are in milliseconds. */
void futo_engine_gesture(FutoEngine *engine, const char *context, const int *xs, const int *ys,
                         const int *times, int n_points, FutoSuggestions *out);

#ifdef __cplusplus
}
#endif
