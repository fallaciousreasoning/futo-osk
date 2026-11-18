/*
 * Built-in layouts, after FUTO Keyboard's Default/qwerty.yaml and
 * Special/{symbols,symbols_shift,number}.yaml with English long-press keys.
 *
 * Each key is written as "base more1 more2 ...". Special keys start with
 * '$', and "@n" sets a key's width.
 */
#include "osk-layout.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *const *rows[6];
    gboolean words;
    gboolean hints; /* show each top-row key's first alternative as a hint */
} LayoutSpec;

#define ROW(...) ((const char *const[]){ __VA_ARGS__, NULL })

static const LayoutSpec specs[OSK_N_LAYERS] = {
    [OSK_LAYER_ALPHA] = {
        .words = TRUE,
        .hints = TRUE,
        .rows = {
            ROW("q 1", "w 2", "e 3 è é ê ë ē", "r 4", "t 5", "y 6", "u 7 ù ú û ü ū",
                "i 8 ì í î ï ī", "o 9 ò ó ô ö œ ø ō õ", "p 0"),
            ROW("a à á â ä æ ã å ā", "s ß", "d", "f", "g", "h", "j", "k", "l"),
            ROW("$shift@1.5", "z", "x", "c ç", "v", "b", "n ñ", "m", "$delete@1.5"),
            ROW("$symbols@1.5", ", ; :", "$space@5", ". ? ! ' \" - … :", "$enter@1.5"),
        },
    },
    [OSK_LAYER_SYMBOLS] = {
        .rows = {
            ROW("1 ¹ ½ ⅓ ¼ ⅛", "2 ² ⅔", "3 ³ ¾ ⅜", "4 ⁴", "5 ⅝", "6", "7 ⅞", "8", "9", "0 ⁿ ∅"),
            ROW("@", "# №", "$ ¢ £ € ¥ ₱", "_", "% ‰", "&", "- _ – — ·", "+ ±", "( < { [", ") > } ]"),
            ROW("$symbols2@1.5", "* † ★", "\" “ ” „ « »", "' ‘ ’ ‚ ‹ ›", ":", ";", "! ¡", "? ¿",
                "$delete@1.5"),
            ROW("$alpha@1.5", ",", "$numbers", "$space@3", "/", ".", "$enter@1.5"),
        },
    },
    [OSK_LAYER_SYMBOLS2] = {
        .rows = {
            ROW("~", "`", "|", "• ♪ ♥ ♠ ♦ ♣", "√", "π Π μ Ω", "÷", "×", "¶ §", "∆"),
            ROW("£", "€", "¥", "¢", "^ ↑ ↓ ← →", "° ′ ″", "=", "{", "}", "\\"),
            ROW("$symbols@1.5", "©", "®", "™", "℅", "[", "]", "<", ">", "$delete@1.5"),
            ROW("$alpha@1.5", ",", "$numbers", "$space@3", "/", ".", "$enter@1.5"),
        },
    },
    [OSK_LAYER_NUMBERS] = {
        .rows = {
            ROW("1", "2", "3", "- + ( )"),
            ROW("4", "5", "6", "$space"),
            ROW("7", "8", "9", "$delete"),
            ROW("$alpha", "0 * #", ". , /", "$enter"),
        },
    },
};

static const struct {
    const char *name;
    OskKeyType type;
} special_keys[] = {
    { "shift", OSK_KEY_SHIFT },     { "delete", OSK_KEY_DELETE },
    { "enter", OSK_KEY_ENTER },     { "space", OSK_KEY_SPACE },
    { "symbols", OSK_KEY_SYMBOLS }, { "symbols2", OSK_KEY_SYMBOLS2 },
    { "alpha", OSK_KEY_ALPHA },     { "numbers", OSK_KEY_NUMBERS },
};

static void parse_key(OskKey *key, const char *spec, gboolean hint) {
    g_autofree char *s = g_strdup(spec);
    key->width = 1.0;

    char *at = s[0] == '$' ? strchr(s, '@') : NULL;
    if (at) {
        *at = '\0';
        key->width = g_ascii_strtod(at + 1, NULL);
    }

    if (s[0] == '$') {
        for (gsize i = 0; i < G_N_ELEMENTS(special_keys); i++)
            if (!strcmp(s + 1, special_keys[i].name)) key->type = special_keys[i].type;
        return;
    }

    char **parts = g_strsplit(s, " ", -1);
    key->type = OSK_KEY_CHAR;
    key->text = g_strdup(parts[0]);
    key->upper = g_utf8_strup(parts[0], -1);
    if (parts[1]) {
        key->more = g_strdupv(parts + 1);
        if (hint) key->hint = g_strdup(parts[1]);
    }
    g_strfreev(parts);
}

OskLayout *osk_layout_new(OskLayer layer) {
    const LayoutSpec *spec = &specs[layer];
    OskLayout *layout = g_new0(OskLayout, 1);
    layout->words = spec->words;

    while (spec->rows[layout->n_rows]) layout->n_rows++;
    layout->rows = g_new0(OskRow, layout->n_rows);

    for (int r = 0; r < layout->n_rows; r++) {
        OskRow *row = &layout->rows[r];
        while (spec->rows[r][row->n_keys]) row->n_keys++;
        row->keys = g_new0(OskKey, row->n_keys);

        double units = 0;
        for (int k = 0; k < row->n_keys; k++) {
            parse_key(&row->keys[k], spec->rows[r][k], spec->hints && r == 0);
            units += row->keys[k].width;
        }
        layout->units = MAX(layout->units, units);
    }
    return layout;
}

void osk_layout_free(OskLayout *layout) {
    if (!layout) return;
    for (int r = 0; r < layout->n_rows; r++) {
        for (int k = 0; k < layout->rows[r].n_keys; k++) {
            OskKey *key = &layout->rows[r].keys[k];
            g_free(key->text);
            g_free(key->upper);
            g_free(key->hint);
            g_strfreev(key->more);
        }
        g_free(layout->rows[r].keys);
    }
    g_free(layout->rows);
    g_free(layout);
}

const char *osk_key_label(const OskKey *key, gboolean shifted) {
    switch (key->type) {
    case OSK_KEY_CHAR: return shifted ? key->upper : key->text;
    case OSK_KEY_SYMBOLS: return "?123";
    case OSK_KEY_SYMBOLS2: return "=\\<";
    case OSK_KEY_ALPHA: return "ABC";
    case OSK_KEY_NUMBERS: return "123";
    default: return NULL; /* drawn as an icon, or blank */
    }
}
