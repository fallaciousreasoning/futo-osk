#include "osk-swipe-helper.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

struct OskSwipeHelper {
    GSubprocess *process;
    GOutputStream *in;
    GDataInputStream *out;
    /* The frame the models expect: the letter rows, scaled to 0..1. */
    double x0, y0, width, height;
};

static char *existing(char *path) {
    if (g_file_test(path, G_FILE_TEST_EXISTS)) return path;
    g_free(path);
    return NULL;
}

OskSwipeHelperConfig *osk_swipe_helper_config_find(const char *vocab) {
    const char *program = g_getenv("FUTO_OSK_SWIPE_DECODER");
    const char *models = g_getenv("FUTO_SWIPE_MODELS");
    OskSwipeHelperConfig *c = g_new0(OskSwipeHelperConfig, 1);
    c->program = existing(g_strdup(program ? program : LIBEXECDIR "/futo-swipe-decoder"));
    if (!models) models = DATADIR "/futo-swipe-decoder";
    c->encoder = existing(g_build_filename(models, "honorable_sturgeon", "model_fp32.pte", NULL));
    c->decoder = existing(g_build_filename(models, "magic_macaw", "model_fp32.pte", NULL));
    c->vocab = vocab ? existing(g_strdup(vocab)) : NULL;
    if (!c->program || !c->encoder || !c->vocab) {
        osk_swipe_helper_config_free(c);
        return NULL;
    }
    return c;
}

void osk_swipe_helper_config_free(OskSwipeHelperConfig *c) {
    if (!c) return;
    g_free(c->program);
    g_free(c->encoder);
    g_free(c->decoder);
    g_free(c->vocab);
    g_free(c);
}

static gboolean send(OskSwipeHelper *self, const char *line) {
    g_autoptr(GError) error = NULL;
    if (!g_output_stream_write_all(self->in, line, strlen(line), NULL, NULL, &error) ||
        !g_output_stream_flush(self->in, NULL, &error)) {
        g_warning("futo-swipe-decoder: %s", error->message);
        return FALSE;
    }
    return TRUE;
}

/* The reply line, or NULL if the helper has gone. */
static char *receive(OskSwipeHelper *self) {
    g_autoptr(GError) error = NULL;
    char *line = g_data_input_stream_read_line_utf8(self->out, NULL, NULL, &error);
    if (!line) g_warning("futo-swipe-decoder: %s", error ? error->message : "exited");
    return line;
}

OskSwipeHelper *osk_swipe_helper_new(const OskSwipeHelperConfig *c) {
    g_autoptr(GError) error = NULL;
    GPtrArray *argv = g_ptr_array_new();
    g_ptr_array_add(argv, c->program);
    g_ptr_array_add(argv, "--encoder");
    g_ptr_array_add(argv, c->encoder);
    if (c->decoder) {
        g_ptr_array_add(argv, "--decoder");
        g_ptr_array_add(argv, c->decoder);
    }
    g_ptr_array_add(argv, "--vocab");
    g_ptr_array_add(argv, c->vocab);
    g_ptr_array_add(argv, NULL);
    GSubprocess *process = g_subprocess_newv((const char *const *)argv->pdata,
                                             G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
                                             &error);
    g_ptr_array_free(argv, TRUE);
    if (!process) {
        g_warning("Could not start %s: %s", c->program, error->message);
        return NULL;
    }

    OskSwipeHelper *self = g_new0(OskSwipeHelper, 1);
    self->process = process;
    self->in = g_subprocess_get_stdin_pipe(process);
    self->out = g_data_input_stream_new(g_subprocess_get_stdout_pipe(process));
    g_autofree char *ready = receive(self);
    if (g_strcmp0(ready, "ready")) {
        g_warning("futo-swipe-decoder didn't start: %s", ready ? ready : "no reply");
        osk_swipe_helper_free(self);
        return NULL;
    }
    return self;
}

void osk_swipe_helper_free(OskSwipeHelper *self) {
    if (!self) return;
    g_subprocess_force_exit(self->process);
    g_object_unref(self->out);
    g_object_unref(self->process);
    g_free(self);
}

static int compare_code(const void *a, const void *b) {
    return ((const FutoKey *)a)->code - ((const FutoKey *)b)->code;
}

gboolean osk_swipe_helper_set_layout(OskSwipeHelper *self, int width, const FutoKey *keys, int n) {
    /* Only letters; the frame spans the rows they're on. */
    double top = G_MAXDOUBLE, bottom = 0;
    int letters = 0;
    for (int i = 0; i < n; i++) {
        if (!g_unichar_isalpha(keys[i].code)) continue;
        top = MIN(top, keys[i].y);
        bottom = MAX(bottom, keys[i].y + keys[i].height);
        letters++;
    }
    if (letters == 0 || width <= 0) return TRUE;
    self->x0 = 0;
    self->y0 = top;
    self->width = width;
    self->height = bottom - top;

    /* The QWERTY decoder model expects the letters in alphabetical order. */
    g_autofree FutoKey *sorted = g_new(FutoKey, letters);
    for (int i = 0, j = 0; i < n; i++)
        if (g_unichar_isalpha(keys[i].code)) sorted[j++] = keys[i];
    qsort(sorted, letters, sizeof(FutoKey), compare_code);

    GString *line = g_string_new(NULL);
    g_string_printf(line, "layout %d", letters);
    for (int i = 0; i < letters; i++) {
        char letter[8] = { 0 };
        g_unichar_to_utf8(g_unichar_tolower(sorted[i].code), letter);
        g_string_append_printf(line, " %s %.4f %.4f", letter,
                               (sorted[i].x + sorted[i].width / 2.0 - self->x0) / self->width,
                               (sorted[i].y + sorted[i].height / 2.0 - self->y0) / self->height);
    }
    g_string_append_c(line, '\n');
    gboolean sent = send(self, line->str);
    g_string_free(line, TRUE);
    g_autofree char *reply = sent ? receive(self) : NULL;
    if (reply && strcmp(reply, "ok")) g_warning("futo-swipe-decoder rejected the layout: %s", reply);
    return reply != NULL;
}

gboolean osk_swipe_helper_decode(OskSwipeHelper *self, const int *xs, const int *ys,
                                 const int *times, int n, FutoSuggestions *out) {
    out->n = 0;
    out->autocorrect = -1;
    if (self->width <= 0) return TRUE;

    GString *line = g_string_new(NULL);
    g_string_printf(line, "swipe %d", n);
    for (int i = 0; i < n; i++)
        g_string_append_printf(line, " %.4f %.4f %d", (xs[i] - self->x0) / self->width,
                               (ys[i] - self->y0) / self->height, times[i]);
    g_string_append_c(line, '\n');
    gboolean sent = send(self, line->str);
    g_string_free(line, TRUE);
    g_autofree char *reply = sent ? receive(self) : NULL;
    if (!reply || !g_str_has_prefix(reply, "words")) return FALSE;

    g_auto(GStrv) words = g_strsplit(reply + strlen("words"), " ", -1);
    int max = G_N_ELEMENTS(out->items);
    for (int i = 0; words[i] && out->n < max; i++) {
        if (!*words[i] || strlen(words[i]) >= FUTO_MAX_WORD_BYTES) continue;
        FutoSuggestion *s = &out->items[out->n];
        g_strlcpy(s->word, words[i], FUTO_MAX_WORD_BYTES);
        s->score = max - out->n; /* best first */
        out->n++;
    }
    if (out->n > 0) out->autocorrect = 0;
    return TRUE;
}
