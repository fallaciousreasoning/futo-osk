/*
 * Where typed text goes: the focused text field, through Wayland's
 * input-method-v2 (or a virtual keyboard when the field doesn't speak
 * text-input), or a local test field.
 */
#pragma once

#include <glib.h>

typedef enum {
    OSK_SPECIAL_BACKSPACE,
    OSK_SPECIAL_RETURN,
    OSK_SPECIAL_LEFT,
    OSK_SPECIAL_RIGHT,
} OskSpecialKey;

/* zwp_text_input_v3 content purposes and hints we look at. */
typedef enum {
    OSK_PURPOSE_NORMAL = 0,
    OSK_PURPOSE_ALPHA = 1,
    OSK_PURPOSE_DIGITS = 2,
    OSK_PURPOSE_NUMBER = 3,
    OSK_PURPOSE_PHONE = 4,
    OSK_PURPOSE_URL = 5,
    OSK_PURPOSE_EMAIL = 6,
    OSK_PURPOSE_NAME = 7,
    OSK_PURPOSE_PASSWORD = 8,
    OSK_PURPOSE_PIN = 9,
    OSK_PURPOSE_DATE = 10,
    OSK_PURPOSE_TIME = 11,
    OSK_PURPOSE_DATETIME = 12,
    OSK_PURPOSE_TERMINAL = 13,
} OskPurpose;

enum {
    OSK_HINT_COMPLETION = 0x1,
    OSK_HINT_SPELLCHECK = 0x2,
    OSK_HINT_AUTO_CAPITALIZATION = 0x4,
    OSK_HINT_LOWERCASE = 0x8,
    OSK_HINT_UPPERCASE = 0x10,
    OSK_HINT_TITLECASE = 0x20,
    OSK_HINT_HIDDEN_TEXT = 0x40,
    OSK_HINT_SENSITIVE_DATA = 0x80,
    OSK_HINT_LATIN = 0x100,
    OSK_HINT_MULTILINE = 0x200,
};

typedef struct OskInput OskInput;

/*
 * Backends get at most one of each of delete_before, commit and
 * set_preedit per flush, in that order, as input-method-v2 applies them:
 * its requests are double-buffered and only the last of each kind counts.
 */
typedef struct {
    void (*commit)(OskInput *input, const char *text);
    void (*set_preedit)(OskInput *input, const char *text);
    /* Deletes this many bytes before the cursor. */
    void (*delete_before)(OskInput *input, int bytes);
    void (*key)(OskInput *input, OskSpecialKey key);
    /* Applies everything since the last flush. */
    void (*flush)(OskInput *input);
    void (*free)(OskInput *input);
} OskInputVTable;

struct OskInput {
    const OskInputVTable *vt;

    /* State of the focused text field, as of the last change notification. */
    gboolean active;
    gboolean has_surrounding;
    char *surrounding; /* UTF-8 */
    int cursor;        /* byte offset into surrounding */
    OskPurpose purpose;
    guint hints;
    gboolean changed_by_app; /* last text change wasn't ours (text_change_cause other) */

    /* Called after any of the state above changes. activated is TRUE when
     * a text field newly gained focus. */
    void (*changed)(OskInput *input, gboolean activated, gpointer data);
    gpointer changed_data;

    /* Batched until the next flush. */
    GString *pending_commit;
    int pending_delete;
    char *preedit;
};

/* Returns NULL when the compositor lacks input-method-v2. */
OskInput *osk_wayland_input_new(void);

/* Types into the given GtkText widget (passed as a GtkWidget*). */
OskInput *osk_test_input_new(gpointer gtk_text);

/* Appends to the text committed at the next flush, replacing the preedit. */
void osk_input_commit(OskInput *input, const char *text);
/* Sets the preedit shown after the next flush. */
void osk_input_set_preedit(OskInput *input, const char *text);
/* Deletes bytes before the cursor, before the next flush's commit. */
void osk_input_delete_before(OskInput *input, int bytes);
/* Flushes, then sends the key. */
void osk_input_key(OskInput *input, OskSpecialKey key);
void osk_input_flush(OskInput *input);

void osk_input_free(OskInput *input);

/* Text before the cursor, or "" when unknown. */
const char *osk_input_text_before(OskInput *input, char **owned);
