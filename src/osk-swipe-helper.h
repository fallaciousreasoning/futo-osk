/*
 * Client for futo-swipe-decoder (swipe-decoder/), which decodes swipes with
 * FUTO Swipe's neural models. It's a separate process because it's
 * GPL-3.0, while futo-osk links FUTO's source-first-licensed code.
 *
 * Blocking; use it from one thread.
 */
#pragma once

#include <glib.h>

#include "futo-engine.h"

typedef struct OskSwipeHelper OskSwipeHelper;

typedef struct {
    char *program;      /* the futo-swipe-decoder executable */
    char *encoder;      /* encoder .pte */
    char *decoder;      /* QWERTY decoder .pte, or NULL */
    char *vocab;        /* AOSP .combined word list */
} OskSwipeHelperConfig;

/* Finds the installed helper and its models, or returns NULL. vocab is
 * the word list to give it. FUTO_OSK_SWIPE_DECODER and FUTO_SWIPE_MODELS
 * override where the program and models are looked for. */
OskSwipeHelperConfig *osk_swipe_helper_config_find(const char *vocab);
void osk_swipe_helper_config_free(OskSwipeHelperConfig *config);

/* Starts the helper and waits for its models to load; NULL on failure. */
OskSwipeHelper *osk_swipe_helper_new(const OskSwipeHelperConfig *config);
void osk_swipe_helper_free(OskSwipeHelper *self);

/* Key geometry as given to the FUTO engine. FALSE if the helper failed. */
gboolean osk_swipe_helper_set_layout(OskSwipeHelper *self, int width, const FutoKey *keys, int n);

/* FALSE if the helper failed; it should then be freed. */
gboolean osk_swipe_helper_decode(OskSwipeHelper *self, const int *xs, const int *ys,
                                 const int *times, int n, FutoSuggestions *out);
