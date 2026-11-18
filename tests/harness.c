/*
 * Scripted typing through the keyboard widget, IME and engine into a test
 * text field, checking what ends up in it. Run under a headless GDK
 * backend (see tests/run-harness.sh).
 *
 *   harness DATA_DIR [SCREENSHOT_PREFIX]
 *
 * Scripts are plain text plus: \b backspace, \n enter, {swipe:word},
 * {pick:N} to tap suggestion slot N (0 left, 1 centre, 2 right),
 * {shot:name} to save a screenshot.
 */
#include <gtk/gtk.h>
#include <string.h>

#include "osk-ime.h"
#include "osk-keyboard.h"

typedef struct {
    const char *script;
    const char *expect;
} Case;

static const Case cases[] = {
    /* autocorrect on space, and auto-capitalisation of the first letter */
    { "i would like to hwlp ", "I would like to help " },
    /* backspace right after an autocorrection restores what was typed */
    { "i would like to hwlp \b", "I would like to hwlp " },
    /* a valid word isn't corrected */
    { "the cat sat ", "The cat sat " },
    /* punctuation ends a word and corrects it */
    { "thanks for the hwlp.", "Thanks for the help." },
    /* double space to period, then capitals again */
    { "see you  then", "See you. Then" },
    /* swipes, each followed by a space */
    { "{swipe:keyboard}{swipe:help}", "Keyboard help " },
    /* a habitual space after a swipe isn't doubled; punctuation takes the space's place */
    { "{swipe:help} ", "Help " },
    { "{swipe:help}.", "Help. " },
    /* enter drops the automatic space */
    { "{swipe:help}\n", "Help\n" },
    /* backspace after a swipe deletes the swiped word */
    { "hello {swipe:world}\b", "Hello " },
    /* picking a prediction, then punctuation sticks to it */
    { "thank {pick:1}.", "Thank you. " },
    /* typing fast: separators wait for the word's suggestions */
    { "i hwve a dog ", "I have a dog " },
    { "i would like to hwl{shot:typing}", NULL },
};

static GtkWindow *window;
static OskKeyboard *keyboard;
static OskIme *ime;
static GtkTextBuffer *buffer;
static const char *shot_prefix;
static gsize case_index;
static const char *cursor;
static int failures;

static void screenshot(const char *name) {
    if (!shot_prefix) return;
    GtkWidget *w = gtk_window_get_child(window);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    int width = gtk_widget_get_width(w), height = gtk_widget_get_height(w);
    /* Broadway's frame clock idles without a browser attached, so lay out by hand. */
    gtk_widget_size_allocate(w, &(GtkAllocation){ 0, 0, width, height }, -1);
    gtk_widget_snapshot_child(GTK_WIDGET(window), w, snapshot);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    GskRenderer *renderer = gsk_cairo_renderer_new();
    gsk_renderer_realize_for_display(renderer, gtk_widget_get_display(w), NULL);
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
    GdkTexture *texture = gsk_renderer_render_texture(renderer, node, &bounds);
    g_autofree char *path = g_strdup_printf("%s-%s.png", shot_prefix, name);
    gdk_texture_save_to_png(texture, path);
    g_print("  screenshot %s\n", path);
    g_object_unref(texture);
    gsk_renderer_unrealize(renderer);
    g_object_unref(renderer);
    gsk_render_node_unref(node);
}

static GtkWidget *slot(int n) {
    GtkWidget *child = gtk_widget_get_first_child(osk_ime_get_bar(ime));
    while (n-- > 0 && child) child = gtk_widget_get_next_sibling(child);
    return child;
}

static void tap(OskKeyType type, const char *text, const char *typed) {
    int x, y;
    OskKey *key = osk_keyboard_find_key(keyboard, type, text, &x, &y);
    if (!key) {
        g_printerr("  no key %s\n", text);
        return;
    }
    g_signal_emit_by_name(keyboard, "key", key, typed, x, y);
}

static void swipe(const char *word) {
    GArray *points = g_array_new(FALSE, FALSE, sizeof(OskGesturePoint));
    int t = 0;
    for (const char *c = word; *c; c++) {
        char s[2] = { *c, 0 };
        int x, y;
        if (!osk_keyboard_find_key(keyboard, OSK_KEY_CHAR, s, &x, &y)) continue;
        if (points->len) {
            OskGesturePoint *last = &g_array_index(points, OskGesturePoint, points->len - 1);
            int lx = last->x, ly = last->y;
            for (int i = 1; i <= 8; i++) {
                OskGesturePoint p = { lx + (x - lx) * i / 8, ly + (y - ly) * i / 8, t += 12 };
                g_array_append_val(points, p);
            }
        } else {
            OskGesturePoint p = { x, y, 0 };
            g_array_append_val(points, p);
        }
    }
    gboolean shifted = osk_keyboard_get_shift(keyboard) != OSK_SHIFT_OFF;
    g_signal_emit_by_name(keyboard, "gesture", points, shifted);
    if (osk_keyboard_get_shift(keyboard) == OSK_SHIFT_ONCE) osk_keyboard_set_shift(keyboard, OSK_SHIFT_OFF);
    g_array_unref(points);
}

static char *buffer_text(void) {
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(buffer, &a, &b);
    return gtk_text_buffer_get_text(buffer, &a, &b, FALSE);
}

static gboolean step(gpointer data);

static gboolean check(gpointer data) {
    const Case *c = &cases[case_index];
    g_autofree char *text = buffer_text();
    gboolean ok = !c->expect || !strcmp(text, c->expect);
    g_print("%s %-36s -> \"%s\"", ok ? "PASS" : "FAIL", g_strescape(c->script, NULL), text);
    if (!ok) g_print("  (expected \"%s\")", c->expect);
    g_print("\n");
    failures += !ok;

    case_index++;
    cursor = NULL;
    if (case_index == G_N_ELEMENTS(cases)) {
        g_application_quit(g_application_get_default());
        return G_SOURCE_REMOVE;
    }
    g_timeout_add(100, step, NULL);
    return G_SOURCE_REMOVE;
}

/* Types one item of the script; waits like a quick typist. */
static gboolean step(gpointer data) {
    const Case *c = &cases[case_index];
    if (!cursor) {
        /* Fresh field: reset the buffer, then let the IME see it. */
        gtk_text_buffer_set_text(buffer, "", -1);
        cursor = c->script;
        g_timeout_add(100, step, NULL);
        return G_SOURCE_REMOVE;
    }
    if (!*cursor) {
        g_timeout_add(400, check, NULL);
        return G_SOURCE_REMOVE;
    }

    int delay = 40;
    if (*cursor == '{') {
        const char *end = strchr(cursor, '}');
        g_autofree char *cmd = g_strndup(cursor + 1, end - cursor - 1);
        cursor = end + 1;
        if (g_str_has_prefix(cmd, "swipe:")) swipe(cmd + 6), delay = 300;
        else if (g_str_has_prefix(cmd, "pick:")) g_signal_emit_by_name(slot(atoi(cmd + 5)), "clicked");
        else if (g_str_has_prefix(cmd, "shot:")) screenshot(cmd + 5);
    } else if (*cursor == '\b') {
        tap(OSK_KEY_DELETE, NULL, NULL);
        cursor++;
    } else if (*cursor == '\n') {
        tap(OSK_KEY_ENTER, NULL, NULL);
        cursor++;
    } else if (*cursor == ' ') {
        tap(OSK_KEY_SPACE, NULL, NULL);
        cursor++;
    } else {
        char lower[2] = { g_ascii_tolower(*cursor), 0 };
        char typed[2] = { *cursor, 0 };
        if (osk_keyboard_get_shift(keyboard) != OSK_SHIFT_OFF) typed[0] = g_ascii_toupper(*cursor);
        /* Characters not on the letter layer, such as '.', come from the
         * period key's long-press list or the symbols layer; send them
         * through the period key. */
        if (g_ascii_isalpha(*cursor) || *cursor == '\'') {
            tap(OSK_KEY_CHAR, lower, typed);
            if (osk_keyboard_get_shift(keyboard) == OSK_SHIFT_ONCE) osk_keyboard_set_shift(keyboard, OSK_SHIFT_OFF);
        } else {
            tap(OSK_KEY_CHAR, ".", typed);
        }
        cursor++;
    }
    g_timeout_add(delay, step, NULL);
    return G_SOURCE_REMOVE;
}

static void activate(GtkApplication *app, char **argv) {
    window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_default_size(window, 540, -1);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *view = gtk_text_view_new();
    gtk_widget_set_size_request(view, -1, 80);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    keyboard = OSK_KEYBOARD(osk_keyboard_new());
    osk_keyboard_set_row_height(keyboard, 54);

    g_autofree char *dict = g_build_filename(argv[1], "en", "main.dict", NULL);
    g_autofree char *model = g_build_filename(argv[1], "en", "model.gguf", NULL);
    /* Swipes go to futo-swipe-decoder if FUTO_OSK_SWIPE_DECODER and
     * FUTO_SWIPE_MODELS point at one, else to the dictionary. */
    g_autofree char *words = g_build_filename(argv[1], "en", "words.combined", NULL);
    OskSwipeHelperConfig *swipe = g_getenv("FUTO_OSK_SWIPE_DECODER") ? osk_swipe_helper_config_find(words) : NULL;
    g_print("swipes: %s\n", swipe ? "futo-swipe-decoder" : "dictionary");
    ime = osk_ime_new(osk_test_input_new(view), keyboard, dict, model, swipe);

    gtk_box_append(GTK_BOX(box), view);
    gtk_box_append(GTK_BOX(box), osk_ime_get_bar(ime));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(keyboard));
    gtk_window_set_child(window, box);
    gtk_window_present(window);
    /* Give the engine time to load. */
    g_timeout_add(1500, step, NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        g_printerr("usage: %s DATA_DIR [SCREENSHOT_PREFIX]\n", argv[0]);
        return 2;
    }
    shot_prefix = argc > 2 ? argv[2] : NULL;
    GtkApplication *app = gtk_application_new(NULL, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), argv);
    char *args[] = { argv[0], NULL };
    g_application_run(G_APPLICATION(app), 1, args);
    g_clear_pointer(&ime, osk_ime_free);
    g_print("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
