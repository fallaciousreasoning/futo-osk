/*
 * Runs the FUTO engine on a worker thread. Only the newest request is
 * served; results arrive on the main thread.
 */
#pragma once

#include <glib.h>

#include "futo-engine.h"

typedef struct OskPredictor OskPredictor;

typedef void (*OskPredictorCallback)(guint request_id, const FutoSuggestions *result,
                                     gpointer data);

/* Loads the engine in the background. lm_path may be NULL. */
OskPredictor *osk_predictor_new(const char *dict_path, const char *lm_path,
                                OskPredictorCallback callback, gpointer data);
void osk_predictor_free(OskPredictor *self);

void osk_predictor_set_layout(OskPredictor *self, int width, int height, const FutoKey *keys,
                              int n_keys);

/* Returns the request's id. xs/ys/times are copied. */
guint osk_predictor_suggest(OskPredictor *self, const char *context, const char *partial,
                            const int *xs, const int *ys, int n);
guint osk_predictor_gesture(OskPredictor *self, const char *context, const int *xs,
                            const int *ys, const int *times, int n);
