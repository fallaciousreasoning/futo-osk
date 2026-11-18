#pragma once

#include <gtk/gtk.h>

#include "futo-engine.h"
#include "osk-layout.h"

G_BEGIN_DECLS

#define OSK_TYPE_KEYBOARD (osk_keyboard_get_type())
G_DECLARE_FINAL_TYPE(OskKeyboard, osk_keyboard, OSK, KEYBOARD, GtkWidget)

typedef enum {
    OSK_SHIFT_OFF,
    OSK_SHIFT_ONCE,
    OSK_SHIFT_LOCKED,
} OskShift;

/* A swipe point in engine coordinates (device pixels) and milliseconds. */
typedef struct {
    int x, y, t;
} OskGesturePoint;

/*
 * Signals:
 *   pressed ()            a finger or button went down on a key
 *   key (OskKey *key, const char *text, int x, int y)
 *       A key was typed. text is what a CHAR key produced (after shift or
 *       a long-press choice), else NULL. x/y are in engine coordinates.
 *   gesture (GArray *points of OskGesturePoint, gboolean shifted)
 *   cursor (int delta)    space bar dragged sideways
 *   geometry-changed ()   engine key positions changed
 */

GtkWidget *osk_keyboard_new(void);

void osk_keyboard_set_layer(OskKeyboard *self, OskLayer layer);
OskLayer osk_keyboard_get_layer(OskKeyboard *self);

void osk_keyboard_set_shift(OskKeyboard *self, OskShift shift);
OskShift osk_keyboard_get_shift(OskKeyboard *self);

void osk_keyboard_set_row_height(OskKeyboard *self, int height);
void osk_keyboard_set_dark(OskKeyboard *self, gboolean dark);

/* The letter keys, for the engine. Returns the number of keys. */
int osk_keyboard_get_engine_keys(OskKeyboard *self, FutoKey *keys, int max_keys, int *width,
                                 int *height);

/* Finds a key on the current layer by type, and for CHAR keys by text.
 * Stores its centre in engine coordinates. Used by tests. */
OskKey *osk_keyboard_find_key(OskKeyboard *self, OskKeyType type, const char *text, int *x, int *y);

G_END_DECLS
