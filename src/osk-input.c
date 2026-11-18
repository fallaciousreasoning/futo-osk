#include "osk-input.h"

static void ensure_pending(OskInput *input) {
    if (!input->pending_commit) input->pending_commit = g_string_new("");
}

void osk_input_commit(OskInput *input, const char *text) {
    ensure_pending(input);
    g_string_append(input->pending_commit, text);
    g_clear_pointer(&input->preedit, g_free);
}

void osk_input_set_preedit(OskInput *input, const char *text) {
    g_free(input->preedit);
    input->preedit = *text ? g_strdup(text) : NULL;
}

void osk_input_delete_before(OskInput *input, int bytes) {
    input->pending_delete += bytes;
}

void osk_input_flush(OskInput *input) {
    ensure_pending(input);
    if (input->pending_delete) input->vt->delete_before(input, input->pending_delete);
    if (input->pending_commit->len) input->vt->commit(input, input->pending_commit->str);
    /* Preedit isn't kept across commits, so send it every time. */
    input->vt->set_preedit(input, input->preedit ? input->preedit : "");
    input->vt->flush(input);
    input->pending_delete = 0;
    g_string_truncate(input->pending_commit, 0);
}

void osk_input_key(OskInput *input, OskSpecialKey key) {
    osk_input_flush(input);
    input->vt->key(input, key);
}

void osk_input_free(OskInput *input) {
    if (!input) return;
    input->vt->free(input);
    g_free(input->surrounding);
    g_free(input->preedit);
    if (input->pending_commit) g_string_free(input->pending_commit, TRUE);
    g_free(input);
}

const char *osk_input_text_before(OskInput *input, char **owned) {
    *owned = NULL;
    if (!input->has_surrounding || !input->surrounding) return "";
    *owned = g_strndup(input->surrounding, input->cursor);
    return *owned;
}
