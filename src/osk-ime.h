/*
 * The typing logic between the key area, the suggestion strip, the FUTO
 * engine and the text field: composing words, autocorrect and its undo,
 * swipes, phantom spaces and auto-capitalisation.
 */
#pragma once

#include <gtk/gtk.h>

#include "osk-input.h"
#include "osk-keyboard.h"

typedef struct OskIme OskIme;

/* Takes ownership of input. dict_path/lm_path may be NULL. */
OskIme *osk_ime_new(OskInput *input, OskKeyboard *keyboard, const char *dict_path,
                    const char *lm_path);
void osk_ime_free(OskIme *self);

/* The suggestion strip, to pack above the keyboard. */
GtkWidget *osk_ime_get_bar(OskIme *self);

/* Called when a text field gains (TRUE) or loses (FALSE) focus. */
void osk_ime_set_focus_callback(OskIme *self, void (*callback)(gboolean focused, gpointer data),
                                gpointer data);
