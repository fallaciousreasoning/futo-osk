#pragma once

#include <graphene.h>
#include <glib.h>

typedef enum {
    OSK_KEY_CHAR,
    OSK_KEY_SHIFT,
    OSK_KEY_DELETE,
    OSK_KEY_ENTER,
    OSK_KEY_SPACE,
    OSK_KEY_SYMBOLS,  /* to the symbols layer */
    OSK_KEY_SYMBOLS2, /* to the second symbols layer */
    OSK_KEY_ALPHA,    /* back to letters */
    OSK_KEY_NUMBERS,  /* to the number pad */
} OskKeyType;

typedef struct {
    OskKeyType type;
    char *text;    /* what a CHAR key types, lowercase */
    char *upper;   /* what it types when shifted */
    char *hint;    /* small label in the corner, or NULL */
    char **more;   /* long-press alternatives, NULL-terminated, may be NULL */
    double width;  /* in units of a standard key */

    /* Set by the keyboard widget when it lays the keys out. */
    graphene_rect_t hit;  /* touch area */
    graphene_rect_t face; /* drawn area */
} OskKey;

typedef struct {
    OskKey *keys;
    int n_keys;
} OskRow;

typedef enum {
    OSK_LAYER_ALPHA,
    OSK_LAYER_SYMBOLS,
    OSK_LAYER_SYMBOLS2,
    OSK_LAYER_NUMBERS,
    OSK_N_LAYERS,
} OskLayer;

typedef struct {
    OskRow *rows;
    int n_rows;
    double units;   /* width of the widest row */
    gboolean words; /* swipe typing and suggestions apply */
} OskLayout;

OskLayout *osk_layout_new(OskLayer layer);
void osk_layout_free(OskLayout *layout);

const char *osk_key_label(const OskKey *key, gboolean shifted);
