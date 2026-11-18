/*
 * Types into a GtkTextView in our own window, standing in for a text
 * field in another app. Preedit is kept in the buffer under an
 * underlined tag, and change notifications arrive asynchronously, like
 * they do over Wayland.
 */
#include "osk-input.h"

#include <gtk/gtk.h>

typedef struct {
    OskInput base;
    GtkTextBuffer *buffer;
    GtkTextTag *preedit_tag;
    GtkTextMark *preedit_start;
    gboolean has_preedit;
    gboolean editing;        /* we're changing the buffer */
    gboolean changed_by_app; /* someone else changed it since the last notify */
    int commits;             /* since the last flush; input-method-v2 keeps only one */
    guint notify_id;
} TestInput;

static void preedit_bounds(TestInput *self, GtkTextIter *start, GtkTextIter *end) {
    gtk_text_buffer_get_iter_at_mark(self->buffer, start, self->preedit_start);
    gtk_text_buffer_get_iter_at_mark(self->buffer, end, gtk_text_buffer_get_insert(self->buffer));
}

static void clear_preedit(TestInput *self) {
    if (!self->has_preedit) return;
    GtkTextIter start, end;
    preedit_bounds(self, &start, &end);
    gtk_text_buffer_delete(self->buffer, &start, &end);
    self->has_preedit = FALSE;
}

static gboolean notify(gpointer data) {
    TestInput *self = data;
    OskInput *base = &self->base;
    self->notify_id = 0;

    GtkTextIter start, end, cursor, p_start, p_end;
    gtk_text_buffer_get_bounds(self->buffer, &start, &end);
    gtk_text_buffer_get_iter_at_mark(self->buffer, &cursor, gtk_text_buffer_get_insert(self->buffer));
    if (self->has_preedit) {
        preedit_bounds(self, &p_start, &p_end);
    } else {
        p_start = p_end = cursor;
    }
    /* The text field's own text excludes the preedit. */
    g_autofree char *before = gtk_text_buffer_get_text(self->buffer, &start, &p_start, FALSE);
    g_autofree char *after = gtk_text_buffer_get_text(self->buffer, &p_end, &end, FALSE);

    g_free(base->surrounding);
    base->surrounding = g_strconcat(before, after, NULL);
    base->cursor = strlen(before);
    base->has_surrounding = TRUE;

    gboolean activated = !base->active;
    base->active = TRUE;
    base->changed_by_app = self->changed_by_app;
    self->changed_by_app = FALSE;
    if (base->changed) base->changed(base, activated, base->changed_data);
    return G_SOURCE_REMOVE;
}

static void queue_notify(TestInput *self) {
    if (!self->notify_id) self->notify_id = g_timeout_add(5, notify, self);
}

static void test_commit(OskInput *input, const char *text) {
    TestInput *self = (TestInput *)input;
    if (++self->commits > 1) g_critical("Two commits in one input-method update; the first would be lost");
    self->editing = TRUE;
    clear_preedit(self);
    gtk_text_buffer_insert_at_cursor(self->buffer, text, -1);
    self->editing = FALSE;
}

static void test_set_preedit(OskInput *input, const char *text) {
    TestInput *self = (TestInput *)input;
    self->editing = TRUE;
    clear_preedit(self);
    if (*text) {
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(self->buffer, &cursor, gtk_text_buffer_get_insert(self->buffer));
        gtk_text_buffer_move_mark(self->buffer, self->preedit_start, &cursor);
        gtk_text_buffer_insert_with_tags(self->buffer, &cursor, text, -1, self->preedit_tag, NULL);
        self->has_preedit = TRUE;
    }
    self->editing = FALSE;
}

static void test_delete_before(OskInput *input, int bytes) {
    TestInput *self = (TestInput *)input;
    self->editing = TRUE;
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_mark(self->buffer, &end, self->has_preedit ? self->preedit_start
                                                                         : gtk_text_buffer_get_insert(self->buffer));
    start = end;
    while (bytes > 0 && gtk_text_iter_backward_char(&start)) {
        char buf[8];
        bytes -= g_unichar_to_utf8(gtk_text_iter_get_char(&start), buf);
    }
    gtk_text_buffer_delete(self->buffer, &start, &end);
    self->editing = FALSE;
}

static void test_key(OskInput *input, OskSpecialKey key) {
    TestInput *self = (TestInput *)input;
    self->editing = TRUE;
    GtkTextIter cursor;
    clear_preedit(self);
    gtk_text_buffer_get_iter_at_mark(self->buffer, &cursor, gtk_text_buffer_get_insert(self->buffer));
    switch (key) {
    case OSK_SPECIAL_BACKSPACE:
        gtk_text_buffer_backspace(self->buffer, &cursor, TRUE, TRUE);
        break;
    case OSK_SPECIAL_RETURN:
        gtk_text_buffer_insert_at_cursor(self->buffer, "\n", 1);
        break;
    case OSK_SPECIAL_LEFT:
        gtk_text_iter_backward_char(&cursor);
        gtk_text_buffer_place_cursor(self->buffer, &cursor);
        break;
    case OSK_SPECIAL_RIGHT:
        gtk_text_iter_forward_char(&cursor);
        gtk_text_buffer_place_cursor(self->buffer, &cursor);
        break;
    }
    self->editing = FALSE;
}

static void test_flush(OskInput *input) {
    ((TestInput *)input)->commits = 0;
    queue_notify((TestInput *)input);
}

static void test_free(OskInput *input) {
    TestInput *self = (TestInput *)input;
    g_clear_handle_id(&self->notify_id, g_source_remove);
    g_signal_handlers_disconnect_by_data(self->buffer, self);
    g_object_unref(self->buffer);
}

static const OskInputVTable test_vtable = {
    test_commit, test_set_preedit, test_delete_before, test_key, test_flush, test_free,
};

static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, TestInput *self) {
    if (!self->editing) self->changed_by_app = TRUE;
    queue_notify(self);
}

static void on_buffer_changed(GtkTextBuffer *buffer, TestInput *self) {
    if (self->editing) return;
    /* The app replaced the text, preedit included. */
    self->has_preedit = FALSE;
    self->changed_by_app = TRUE;
    queue_notify(self);
}

OskInput *osk_test_input_new(gpointer text_view) {
    TestInput *self = g_new0(TestInput, 1);
    self->base.vt = &test_vtable;
    self->base.hints = OSK_HINT_AUTO_CAPITALIZATION | OSK_HINT_SPELLCHECK | OSK_HINT_COMPLETION;
    self->buffer = g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view)));
    self->preedit_tag = gtk_text_buffer_create_tag(self->buffer, NULL, "underline",
                                                   PANGO_UNDERLINE_SINGLE, NULL);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(self->buffer, &start);
    self->preedit_start = gtk_text_buffer_create_mark(self->buffer, NULL, &start, TRUE);
    g_signal_connect(self->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), self);
    g_signal_connect(self->buffer, "changed", G_CALLBACK(on_buffer_changed), self);
    queue_notify(self);
    return &self->base;
}
