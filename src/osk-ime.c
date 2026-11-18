#include "osk-ime.h"

#include <string.h>

#include "osk-predictor.h"

#define N_SLOTS 3
#define DOUBLE_SPACE_MS 1000
#define SEPARATOR_WAIT_MS 250

/* A key press waiting for suggestions to catch up. */
typedef struct {
    OskKey *key;
    char *text;
    int x, y;
} Action;

struct OskIme {
    OskInput *input;
    OskKeyboard *keyboard;
    OskPredictor *predictor;

    GtkWidget *bar;
    GtkWidget *slots[N_SLOTS];
    char *slot_words[N_SLOTS];

    /* The word being typed, shown as preedit, and where each of its
     * characters was tapped. */
    GString *word;
    GArray *xs, *ys;

    /* Suggestions, and what they were computed for. */
    guint request_id;
    gboolean waiting;
    FutoSuggestions suggestions;
    char *suggested_for; /* the word, or "" for next-word predictions */

    /* Typed while waiting for suggestions for the current word. */
    GQueue pending;
    guint pending_timeout;

    /* Text we committed that the text field hasn't reported back yet. */
    GString *unsynced;

    /* Undoing an autocorrection with backspace. */
    char *undo_typed;     /* what was typed */
    int undo_bytes;       /* bytes committed in its place, separator included */
    char *undo_separator;

    char *swiped;           /* last swiped word, replaceable from the strip */
    gboolean auto_space;    /* we typed a space after the last word; punctuation can take its place */
    gint64 last_space;      /* for double-space to period */
    gboolean auto_shifted;

    void (*focus_cb)(gboolean, gpointer);
    gpointer focus_data;
};

static void on_key(OskKeyboard *kb, OskKey *key, const char *text, int x, int y, OskIme *self);

/* --- Field properties --- */

static gboolean field_is_private(OskIme *self) {
    OskInput *in = self->input;
    return in->purpose == OSK_PURPOSE_PASSWORD || in->purpose == OSK_PURPOSE_PIN ||
           in->purpose == OSK_PURPOSE_TERMINAL ||
           (in->hints & (OSK_HINT_HIDDEN_TEXT | OSK_HINT_SENSITIVE_DATA));
}

/* Composing and suggestions need input-method support from the field. */
static gboolean suggestions_on(OskIme *self) {
    return self->input->active && !field_is_private(self) && self->predictor;
}

static gboolean autocorrect_on(OskIme *self) {
    OskPurpose p = self->input->purpose;
    return suggestions_on(self) && p != OSK_PURPOSE_URL && p != OSK_PURPOSE_EMAIL &&
           p != OSK_PURPOSE_NAME;
}

static gboolean is_word_char(gunichar c) {
    return g_unichar_isalnum(c) || c == '\'' || g_unichar_ismark(c);
}

static gboolean is_autocorrect_separator(const char *text) {
    return text && strchr(".,!?;:)\"", text[0]) && text[1] == '\0';
}

/* Punctuation that takes the place of an automatic space after a word. */
static gboolean is_attaching_punctuation(const char *text) {
    return text && strchr(".,!?;:)", text[0]) && text[1] == '\0';
}

/* --- Context --- */

static char *context_before(OskIme *self) {
    g_autofree char *owned = NULL;
    const char *before = osk_input_text_before(self->input, &owned);
    return g_strconcat(before, self->unsynced->str, NULL);
}

static void commit(OskIme *self, const char *text) {
    osk_input_commit(self->input, text);
    g_string_append(self->unsynced, text);
}

static void delete_before(OskIme *self, int bytes) {
    osk_input_delete_before(self->input, bytes);
    g_string_truncate(self->unsynced, MAX(0, (int)self->unsynced->len - bytes));
}

static void update_shift(OskIme *self) {
    OskInput *in = self->input;
    if (osk_keyboard_get_shift(self->keyboard) == OSK_SHIFT_LOCKED) return;

    gboolean caps = FALSE;
    gboolean allowed = (in->hints & OSK_HINT_AUTO_CAPITALIZATION) ||
                       (in->hints == 0 && in->purpose == OSK_PURPOSE_NORMAL);
    if (in->active && in->has_surrounding && allowed && !(in->hints & OSK_HINT_LOWERCASE) &&
        self->word->len == 0) {
        g_autofree char *before = context_before(self);
        int n = strlen(before), i = n;
        while (i > 0 && before[i - 1] == ' ') i--;
        caps = i == 0 || before[i - 1] == '\n' ||
               (i < n && strchr(".!?", before[i - 1]));
    }

    if (caps) {
        if (osk_keyboard_get_shift(self->keyboard) == OSK_SHIFT_OFF) {
            osk_keyboard_set_shift(self->keyboard, OSK_SHIFT_ONCE);
            self->auto_shifted = TRUE;
        }
    } else if (self->auto_shifted && osk_keyboard_get_shift(self->keyboard) == OSK_SHIFT_ONCE) {
        osk_keyboard_set_shift(self->keyboard, OSK_SHIFT_OFF);
    }
    if (osk_keyboard_get_shift(self->keyboard) == OSK_SHIFT_OFF) self->auto_shifted = FALSE;
}

/* --- Suggestion strip --- */

static void set_slot(OskIme *self, int i, const char *word, gboolean primary) {
    g_free(self->slot_words[i]);
    self->slot_words[i] = g_strdup(word);
    GtkWidget *label = gtk_button_get_child(GTK_BUTTON(self->slots[i]));
    gtk_label_set_text(GTK_LABEL(label), word ? word : "");
    gtk_widget_set_sensitive(self->slots[i], word != NULL);
    if (primary) gtk_widget_add_css_class(self->slots[i], "autocorrect");
    else gtk_widget_remove_css_class(self->slots[i], "autocorrect");
}

static void clear_strip(OskIme *self) {
    for (int i = 0; i < N_SLOTS; i++) set_slot(self, i, NULL, FALSE);
}

/* Centre: the word a separator would produce. Sides: the alternatives. */
static void show_suggestions(OskIme *self) {
    const FutoSuggestions *s = &self->suggestions;
    const char *typed = self->word->len ? self->word->str : NULL;
    const char *centre = NULL;
    gboolean will_correct = FALSE;

    if (typed) {
        will_correct = s->autocorrect >= 0 && autocorrect_on(self);
        centre = will_correct ? s->items[s->autocorrect].word : typed;
    } else if (s->n > 0) {
        centre = s->items[0].word;
    }

    const char *others[N_SLOTS];
    int n = 0;
    if (typed && will_correct) others[n++] = typed;
    for (int i = 0; i < s->n && n < N_SLOTS - 1; i++) {
        const char *w = s->items[i].word;
        if (centre && !strcmp(w, centre)) continue;
        if (typed && !strcmp(w, typed)) continue;
        others[n++] = w;
    }

    set_slot(self, 1, centre, will_correct);
    set_slot(self, 0, n > 0 ? others[0] : NULL, FALSE);
    set_slot(self, 2, n > 1 ? others[1] : NULL, FALSE);
}

static void request_suggestions(OskIme *self) {
    if (!suggestions_on(self)) {
        self->waiting = FALSE;
        clear_strip(self);
        return;
    }
    g_autofree char *context = context_before(self);
    self->request_id = osk_predictor_suggest(self->predictor, context, self->word->str,
                                             (int *)self->xs->data, (int *)self->ys->data,
                                             self->xs->len);
    g_free(self->suggested_for);
    self->suggested_for = g_strdup(self->word->str);
    self->waiting = TRUE;
}

/* --- Pending actions --- */

static void action_free(gpointer data) {
    Action *a = data;
    g_free(a->text);
    g_free(a);
}

static void run_pending(OskIme *self) {
    g_clear_handle_id(&self->pending_timeout, g_source_remove);
    Action *a;
    while (!self->waiting && (a = g_queue_pop_head(&self->pending))) {
        on_key(self->keyboard, a->key, a->text, a->x, a->y, self);
        action_free(a);
    }
}

static gboolean pending_timed_out(gpointer data) {
    OskIme *self = data;
    self->pending_timeout = 0;
    self->waiting = FALSE;
    run_pending(self);
    return G_SOURCE_REMOVE;
}

static void on_suggestions(guint id, const FutoSuggestions *result, gpointer data) {
    OskIme *self = data;
    if (id != self->request_id) return;
    self->suggestions = *result;
    self->waiting = FALSE;
    show_suggestions(self);
    run_pending(self);
}

/* --- Editing --- */

static void reset_word(OskIme *self) {
    g_string_truncate(self->word, 0);
    g_array_set_size(self->xs, 0);
    g_array_set_size(self->ys, 0);
}

static void forget_undo(OskIme *self) {
    g_clear_pointer(&self->undo_typed, g_free);
    g_clear_pointer(&self->undo_separator, g_free);
    g_clear_pointer(&self->swiped, g_free);
}

static void flush(OskIme *self) {
    osk_input_flush(self->input);
    update_shift(self);
}

/* Commits the word being typed, corrected if allowed and confident. */
static void finish_word(OskIme *self, gboolean correct, const char *separator) {
    if (self->word->len == 0) {
        if (separator) commit(self, separator);
        return;
    }
    const char *out = self->word->str;
    const FutoSuggestions *s = &self->suggestions;
    gboolean fresh = !self->waiting && self->suggested_for && !strcmp(self->suggested_for, self->word->str);
    if (correct && fresh && autocorrect_on(self) && s->autocorrect >= 0)
        out = s->items[s->autocorrect].word;

    commit(self, out);
    if (separator) commit(self, separator);
    if (strcmp(out, self->word->str)) {
        self->undo_typed = g_strdup(self->word->str);
        self->undo_separator = g_strdup(separator ? separator : "");
        self->undo_bytes = strlen(out) + strlen(self->undo_separator);
    }
    reset_word(self);
}

static void type_word_char(OskIme *self, const char *text, int x, int y) {
    self->auto_space = FALSE;
    forget_undo(self);
    if (!suggestions_on(self)) {
        commit(self, text);
        return;
    }
    g_string_append(self->word, text);
    for (const char *p = text; *p; p = g_utf8_next_char(p)) {
        g_array_append_val(self->xs, x);
        g_array_append_val(self->ys, y);
    }
    osk_input_set_preedit(self->input, self->word->str);
    request_suggestions(self);
}

static void type_separator(OskIme *self, const char *text) {
    /* After a swipe or a picked suggestion, "word " + "." becomes "word. " */
    gboolean reattach = self->auto_space && self->word->len == 0 && is_attaching_punctuation(text);
    self->auto_space = FALSE;
    forget_undo(self);
    if (reattach) delete_before(self, 1);
    finish_word(self, is_autocorrect_separator(text), text);
    if (reattach) {
        commit(self, " ");
        self->auto_space = TRUE;
    }
    request_suggestions(self);
}

static void type_space(OskIme *self) {
    gint64 now = g_get_monotonic_time();
    gboolean composing = self->word->len > 0;

    if (self->auto_space && !composing) {
        /* The space is already there; a habitual tap shouldn't double it,
         * but a second tap still makes a period. */
        self->auto_space = FALSE;
        forget_undo(self);
        self->last_space = now;
        request_suggestions(self);
        return;
    }
    self->auto_space = FALSE;

    if (composing) {
        forget_undo(self);
        finish_word(self, TRUE, " ");
    } else if (now - self->last_space < DOUBLE_SPACE_MS * 1000 && !self->undo_typed &&
               self->input->purpose == OSK_PURPOSE_NORMAL) {
        /* Double space: "word " becomes "word. " */
        g_autofree char *before = context_before(self);
        int n = strlen(before);
        if (n >= 2 && before[n - 1] == ' ' && g_unichar_isalnum(g_utf8_get_char(g_utf8_prev_char(before + n - 1)))) {
            delete_before(self, 1);
            commit(self, ". ");
            now = 0;
        } else {
            commit(self, " ");
        }
        forget_undo(self);
    } else {
        forget_undo(self);
        commit(self, " ");
    }
    self->last_space = now;
    request_suggestions(self);
}

static void type_backspace(OskIme *self) {
    int auto_space = self->auto_space ? 1 : 0;
    self->auto_space = FALSE;
    if (self->word->len > 0) {
        const char *last = g_utf8_find_prev_char(self->word->str, self->word->str + self->word->len);
        g_string_truncate(self->word, last - self->word->str);
        g_array_set_size(self->xs, self->xs->len - 1);
        g_array_set_size(self->ys, self->ys->len - 1);
        osk_input_set_preedit(self->input, self->word->str);
        request_suggestions(self);
        return;
    }
    if (self->undo_typed) {
        /* Revert the autocorrection, keeping what was typed. */
        delete_before(self, self->undo_bytes);
        g_autofree char *restored = g_strconcat(self->undo_typed, self->undo_separator, NULL);
        commit(self, restored);
        forget_undo(self);
        request_suggestions(self);
        return;
    }
    if (self->swiped) {
        /* Like AOSP, backspace after a swipe takes the whole word. */
        delete_before(self, strlen(self->swiped) + auto_space);
        forget_undo(self);
        request_suggestions(self);
        return;
    }
    osk_input_key(self->input, OSK_SPECIAL_BACKSPACE);
    if (self->unsynced->len) {
        const char *last = g_utf8_find_prev_char(self->unsynced->str, self->unsynced->str + self->unsynced->len);
        g_string_truncate(self->unsynced, last - self->unsynced->str);
    }
    request_suggestions(self);
}

static void type_enter(OskIme *self) {
    /* Don't leave the automatic space at the end of a line or message. */
    if (self->auto_space && self->word->len == 0) delete_before(self, 1);
    self->auto_space = FALSE;
    forget_undo(self);
    finish_word(self, TRUE, NULL);
    osk_input_flush(self->input);
    osk_input_key(self->input, OSK_SPECIAL_RETURN);
    g_string_truncate(self->unsynced, 0);
    request_suggestions(self);
}

static void on_key(OskKeyboard *kb, OskKey *key, const char *text, int x, int y, OskIme *self) {
    gboolean word_char = key->type == OSK_KEY_CHAR && text && is_word_char(g_utf8_get_char(text)) &&
                         osk_keyboard_get_layer(kb) == OSK_LAYER_ALPHA;
    gboolean separator = !word_char && key->type != OSK_KEY_DELETE;

    /* A separator must wait for suggestions for the word it ends, and
     * everything after it must wait its turn. */
    if (!g_queue_is_empty(&self->pending) ||
        (separator && self->waiting && self->word->len > 0)) {
        Action *a = g_new0(Action, 1);
        *a = (Action){ key, g_strdup(text), x, y };
        g_queue_push_tail(&self->pending, a);
        if (!self->pending_timeout)
            self->pending_timeout = g_timeout_add(SEPARATOR_WAIT_MS, pending_timed_out, self);
        return;
    }

    switch (key->type) {
    case OSK_KEY_CHAR:
        if (word_char) type_word_char(self, text, x, y);
        else type_separator(self, text);
        break;
    case OSK_KEY_SPACE: type_space(self); break;
    case OSK_KEY_DELETE: type_backspace(self); break;
    case OSK_KEY_ENTER: type_enter(self); break;
    default: return;
    }
    flush(self);
}

static void on_gesture(OskKeyboard *kb, GArray *points, gboolean shifted, OskIme *self) {
    if (!suggestions_on(self) || points->len < 2) return;
    if (self->word->len) {
        finish_word(self, FALSE, " ");
        self->auto_space = TRUE;
    }
    forget_undo(self);
    /* Remember the shift for when the result arrives. */
    g_object_set_data(G_OBJECT(kb), "gesture-shifted", GINT_TO_POINTER(shifted));

    int n = points->len;
    g_autofree int *xs = g_new(int, n), *ys = g_new(int, n), *ts = g_new(int, n);
    for (int i = 0; i < n; i++) {
        OskGesturePoint *p = &g_array_index(points, OskGesturePoint, i);
        xs[i] = p->x;
        ys[i] = p->y;
        ts[i] = p->t;
    }
    g_autofree char *context = context_before(self);
    self->request_id = osk_predictor_gesture(self->predictor, context, xs, ys, ts, n);
    g_free(self->suggested_for);
    self->suggested_for = NULL; /* marks a gesture */
    self->waiting = TRUE;
    flush(self);
}

static void on_gesture_result(OskIme *self) {
    FutoSuggestions *s = &self->suggestions;
    if (s->n == 0) return;
    if (g_object_get_data(G_OBJECT(self->keyboard), "gesture-shifted")) {
        for (int i = 0; i < s->n; i++) {
            char *w = s->items[i].word;
            gunichar c = g_unichar_toupper(g_utf8_get_char(w));
            char buf[8] = { 0 };
            g_unichar_to_utf8(c, buf);
            g_autofree char *up = g_strconcat(buf, g_utf8_next_char(w), NULL);
            g_strlcpy(w, up, FUTO_MAX_WORD_BYTES);
        }
    }
    g_autofree char *before = context_before(self);
    int n = strlen(before);
    gunichar prev = n > 0 ? g_utf8_get_char(g_utf8_prev_char(before + n)) : 0;
    if (is_word_char(prev) || (prev && strchr(".,!?;:)", prev)))
        commit(self, " ");
    commit(self, s->items[0].word);
    commit(self, " ");
    self->swiped = g_strdup(s->items[0].word);
    self->auto_space = TRUE;
    flush(self);
}

static void on_predictor_result(guint id, const FutoSuggestions *result, gpointer data) {
    OskIme *self = data;
    if (id == self->request_id && !self->suggested_for) {
        self->suggestions = *result;
        self->waiting = FALSE;
        on_gesture_result(self);
        /* Alternatives to the swiped word. */
        if (self->suggestions.n > 0) {
            set_slot(self, 1, self->suggestions.items[0].word, FALSE);
            set_slot(self, 0, self->suggestions.n > 1 ? self->suggestions.items[1].word : NULL, FALSE);
            set_slot(self, 2, self->suggestions.n > 2 ? self->suggestions.items[2].word : NULL, FALSE);
        }
        run_pending(self);
        return;
    }
    on_suggestions(id, result, data);
}

static void on_slot_clicked(GtkButton *button, OskIme *self) {
    int slot = -1;
    for (int i = 0; i < N_SLOTS; i++)
        if (self->slots[i] == GTK_WIDGET(button)) slot = i;
    if (slot < 0 || !self->slot_words[slot]) return;
    g_autofree char *word = g_strdup(self->slot_words[slot]);

    if (self->swiped) {
        delete_before(self, strlen(self->swiped) + (self->auto_space ? 1 : 0));
        commit(self, word);
        commit(self, " ");
        g_free(self->swiped);
        self->swiped = g_strdup(word);
        self->auto_space = TRUE;
        flush(self);
        return;
    }
    forget_undo(self);
    commit(self, word); /* replaces any preedit */
    commit(self, " ");
    reset_word(self);
    self->auto_space = TRUE;
    flush(self);
    request_suggestions(self);
}

static void on_cursor(OskKeyboard *kb, int delta, OskIme *self) {
    if (self->word->len) {
        finish_word(self, FALSE, NULL);
        osk_input_flush(self->input);
    }
    forget_undo(self);
    self->auto_space = FALSE;
    osk_input_key(self->input, delta < 0 ? OSK_SPECIAL_LEFT : OSK_SPECIAL_RIGHT);
    osk_input_flush(self->input);
}

static void on_geometry_changed(OskKeyboard *kb, OskIme *self) {
    if (!self->predictor) return;
    FutoKey keys[64];
    int w, h;
    int n = osk_keyboard_get_engine_keys(kb, keys, G_N_ELEMENTS(keys), &w, &h);
    osk_predictor_set_layout(self->predictor, w, h, keys, n);
}

static void on_input_changed(OskInput *input, gboolean activated, gpointer data) {
    OskIme *self = data;
    g_string_truncate(self->unsynced, 0);

    if (activated) {
        reset_word(self);
        forget_undo(self);
        self->auto_space = FALSE;
        switch (input->purpose) {
        case OSK_PURPOSE_DIGITS:
        case OSK_PURPOSE_NUMBER:
        case OSK_PURPOSE_PHONE:
        case OSK_PURPOSE_PIN:
            osk_keyboard_set_layer(self->keyboard, OSK_LAYER_NUMBERS);
            break;
        default:
            osk_keyboard_set_layer(self->keyboard, OSK_LAYER_ALPHA);
        }
        if (self->focus_cb) self->focus_cb(TRUE, self->focus_data);
        request_suggestions(self);
    } else if (!input->active) {
        reset_word(self);
        forget_undo(self);
        g_queue_clear_full(&self->pending, action_free);
        clear_strip(self);
        if (self->focus_cb) self->focus_cb(FALSE, self->focus_data);
    } else if (input->changed_by_app && self->word->len) {
        /* The app moved the cursor or changed the text under us. */
        reset_word(self);
        forget_undo(self);
        osk_input_set_preedit(input, "");
        osk_input_flush(input);
        request_suggestions(self);
    } else if (input->changed_by_app) {
        forget_undo(self);
        self->auto_space = FALSE;
    }
    update_shift(self);
}

OskIme *osk_ime_new(OskInput *input, OskKeyboard *keyboard, const char *dict_path,
                    const char *lm_path) {
    OskIme *self = g_new0(OskIme, 1);
    self->input = input;
    self->keyboard = keyboard;
    self->word = g_string_new("");
    self->unsynced = g_string_new("");
    self->xs = g_array_new(FALSE, FALSE, sizeof(int));
    self->ys = g_array_new(FALSE, FALSE, sizeof(int));
    g_queue_init(&self->pending);
    if (dict_path) self->predictor = osk_predictor_new(dict_path, lm_path, on_predictor_result, self);

    self->bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(self->bar, "suggestions");
    gtk_widget_set_hexpand(self->bar, TRUE);
    gtk_box_set_homogeneous(GTK_BOX(self->bar), TRUE);
    for (int i = 0; i < N_SLOTS; i++) {
        GtkWidget *label = gtk_label_new(NULL);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        self->slots[i] = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(self->slots[i]), label);
        gtk_widget_add_css_class(self->slots[i], "flat");
        gtk_widget_add_css_class(self->slots[i], i == 1 ? "centre" : "side");
        gtk_widget_set_focusable(self->slots[i], FALSE);
        g_signal_connect(self->slots[i], "clicked", G_CALLBACK(on_slot_clicked), self);
        gtk_box_append(GTK_BOX(self->bar), self->slots[i]);
    }
    clear_strip(self);

    g_signal_connect(keyboard, "key", G_CALLBACK(on_key), self);
    g_signal_connect(keyboard, "gesture", G_CALLBACK(on_gesture), self);
    g_signal_connect(keyboard, "cursor", G_CALLBACK(on_cursor), self);
    g_signal_connect(keyboard, "geometry-changed", G_CALLBACK(on_geometry_changed), self);
    input->changed = on_input_changed;
    input->changed_data = self;
    return self;
}

void osk_ime_free(OskIme *self) {
    g_signal_handlers_disconnect_by_data(self->keyboard, self);
    g_clear_handle_id(&self->pending_timeout, g_source_remove);
    g_queue_clear_full(&self->pending, action_free);
    if (self->predictor) osk_predictor_free(self->predictor);
    osk_input_free(self->input);
    g_string_free(self->word, TRUE);
    g_string_free(self->unsynced, TRUE);
    g_array_unref(self->xs);
    g_array_unref(self->ys);
    forget_undo(self);
    for (int i = 0; i < N_SLOTS; i++) g_free(self->slot_words[i]);
    g_free(self->suggested_for);
    g_free(self);
}

GtkWidget *osk_ime_get_bar(OskIme *self) { return self->bar; }

void osk_ime_set_focus_callback(OskIme *self, void (*callback)(gboolean, gpointer), gpointer data) {
    self->focus_cb = callback;
    self->focus_data = data;
}
