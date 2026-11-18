/*
 * input-method-v2 for text fields that support text-input-v3, and a
 * virtual keyboard for special keys and for everything else.
 */
#define _GNU_SOURCE
#include "osk-input.h"

#include <gdk/wayland/gdkwayland.h>
#include <gtk/gtk.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

/* Keycodes 9..255 are ours to assign. */
#define FIRST_KEYCODE 9
#define MAX_KEYSYMS (256 - FIRST_KEYCODE)

typedef struct {
    OskInput base;

    struct wl_display *display;
    struct zwp_input_method_manager_v2 *im_manager;
    struct zwp_virtual_keyboard_manager_v1 *vk_manager;
    struct zwp_input_method_v2 *im;
    struct zwp_virtual_keyboard_v1 *vk;
    guint32 serial;

    /* Double-buffered state, applied on done. */
    struct {
        gboolean active;
        gboolean has_surrounding;
        char *surrounding;
        int cursor;
        guint32 hints, purpose;
        gboolean changed_by_app;
    } pending;

    /* The virtual keyboard's keymap: keycode FIRST_KEYCODE + i types keysyms[i]. */
    xkb_keysym_t keysyms[MAX_KEYSYMS];
    int n_keysyms;
    gboolean keymap_dirty;
} WaylandInput;

static const xkb_keysym_t special_keysyms[] = {
    [OSK_SPECIAL_BACKSPACE] = XKB_KEY_BackSpace,
    [OSK_SPECIAL_RETURN] = XKB_KEY_Return,
    [OSK_SPECIAL_LEFT] = XKB_KEY_Left,
    [OSK_SPECIAL_RIGHT] = XKB_KEY_Right,
};

static void upload_keymap(WaylandInput *self) {
    GString *km = g_string_new("xkb_keymap {\n"
                               "xkb_keycodes \"futo-osk\" {\n minimum = 8;\n maximum = 255;\n");
    for (int i = 0; i < self->n_keysyms; i++)
        g_string_append_printf(km, " <I%d> = %d;\n", FIRST_KEYCODE + i, FIRST_KEYCODE + i);
    g_string_append(km, "};\n"
                        "xkb_types \"futo-osk\" { include \"complete\" };\n"
                        "xkb_compatibility \"futo-osk\" { include \"complete\" };\n"
                        "xkb_symbols \"futo-osk\" {\n");
    for (int i = 0; i < self->n_keysyms; i++) {
        char name[64];
        xkb_keysym_get_name(self->keysyms[i], name, sizeof(name));
        g_string_append_printf(km, " key <I%d> { [ %s ] };\n", FIRST_KEYCODE + i, name);
    }
    g_string_append(km, "};\n};\n");

    int fd = memfd_create("futo-osk-keymap", MFD_CLOEXEC);
    if (fd < 0 || write(fd, km->str, km->len + 1) != (ssize_t)km->len + 1) {
        g_warning("Could not write the virtual keyboard keymap");
    } else {
        zwp_virtual_keyboard_v1_keymap(self->vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, km->len + 1);
        self->keymap_dirty = FALSE;
    }
    if (fd >= 0) close(fd);
    g_string_free(km, TRUE);
}

static int keycode_for(WaylandInput *self, xkb_keysym_t sym) {
    for (int i = 0; i < self->n_keysyms; i++)
        if (self->keysyms[i] == sym) return FIRST_KEYCODE + i;

    /* Out of keycodes: recycle the ones past the fixed ASCII set. */
    if (self->n_keysyms == MAX_KEYSYMS) self->n_keysyms = G_N_ELEMENTS(special_keysyms) + 95;
    self->keysyms[self->n_keysyms] = sym;
    self->keymap_dirty = TRUE;
    return FIRST_KEYCODE + self->n_keysyms++;
}

static void tap_keysym(WaylandInput *self, xkb_keysym_t sym) {
    if (!self->vk) return;
    int keycode = keycode_for(self, sym);
    if (self->keymap_dirty) upload_keymap(self);
    guint32 time = g_get_monotonic_time() / 1000;
    /* evdev codes are xkb keycodes minus 8 */
    zwp_virtual_keyboard_v1_key(self->vk, time, keycode - 8, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(self->vk, time, keycode - 8, WL_KEYBOARD_KEY_STATE_RELEASED);
}

static void wl_commit(OskInput *input, const char *text) {
    WaylandInput *self = (WaylandInput *)input;
    if (self->im && input->active) {
        zwp_input_method_v2_commit_string(self->im, text);
        return;
    }
    for (const char *p = text; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        tap_keysym(self, c == '\n' ? XKB_KEY_Return : xkb_utf32_to_keysym(c));
    }
}

static void wl_set_preedit(OskInput *input, const char *text) {
    WaylandInput *self = (WaylandInput *)input;
    if (self->im && input->active) {
        int len = strlen(text);
        zwp_input_method_v2_set_preedit_string(self->im, text, len, len);
    }
}

static void wl_delete_before(OskInput *input, int bytes) {
    WaylandInput *self = (WaylandInput *)input;
    if (self->im && input->active) zwp_input_method_v2_delete_surrounding_text(self->im, bytes, 0);
}

static void wl_key(OskInput *input, OskSpecialKey key) {
    tap_keysym((WaylandInput *)input, special_keysyms[key]);
}

static void wl_flush(OskInput *input) {
    WaylandInput *self = (WaylandInput *)input;
    if (self->im && input->active) zwp_input_method_v2_commit(self->im, self->serial);
    wl_display_flush(self->display);
}

static void wl_free(OskInput *input) {
    WaylandInput *self = (WaylandInput *)input;
    g_clear_pointer(&self->im, zwp_input_method_v2_destroy);
    g_clear_pointer(&self->vk, zwp_virtual_keyboard_v1_destroy);
    g_clear_pointer(&self->im_manager, zwp_input_method_manager_v2_destroy);
    g_clear_pointer(&self->vk_manager, zwp_virtual_keyboard_manager_v1_destroy);
    g_free(self->pending.surrounding);
}

static const OskInputVTable wayland_vtable = {
    wl_commit, wl_set_preedit, wl_delete_before, wl_key, wl_flush, wl_free,
};

static void im_activate(void *data, struct zwp_input_method_v2 *im) {
    WaylandInput *self = data;
    /* Activation resets all state. */
    g_clear_pointer(&self->pending.surrounding, g_free);
    self->pending.has_surrounding = FALSE;
    self->pending.cursor = 0;
    self->pending.hints = 0;
    self->pending.purpose = 0;
    self->pending.changed_by_app = FALSE;
    self->pending.active = TRUE;
}

static void im_deactivate(void *data, struct zwp_input_method_v2 *im) {
    ((WaylandInput *)data)->pending.active = FALSE;
}

static void im_surrounding_text(void *data, struct zwp_input_method_v2 *im, const char *text,
                                uint32_t cursor, uint32_t anchor) {
    WaylandInput *self = data;
    g_free(self->pending.surrounding);
    self->pending.surrounding = g_strdup(text);
    self->pending.cursor = MIN(cursor, strlen(text));
    self->pending.has_surrounding = TRUE;
}

static void im_text_change_cause(void *data, struct zwp_input_method_v2 *im, uint32_t cause) {
    ((WaylandInput *)data)->pending.changed_by_app = cause != 0; /* 0 = input_method */
}

static void im_content_type(void *data, struct zwp_input_method_v2 *im, uint32_t hint,
                            uint32_t purpose) {
    WaylandInput *self = data;
    self->pending.hints = hint;
    self->pending.purpose = purpose;
}

static void im_done(void *data, struct zwp_input_method_v2 *im) {
    WaylandInput *self = data;
    OskInput *base = &self->base;
    gboolean activated = self->pending.active && !base->active;

    self->serial++;
    base->active = self->pending.active;
    base->has_surrounding = self->pending.has_surrounding;
    g_free(base->surrounding);
    base->surrounding = g_strdup(self->pending.surrounding ? self->pending.surrounding : "");
    base->cursor = self->pending.cursor;
    base->hints = self->pending.hints;
    base->purpose = self->pending.purpose;
    base->changed_by_app = self->pending.changed_by_app;
    self->pending.changed_by_app = FALSE;

    if (base->changed) base->changed(base, activated, base->changed_data);
}

static void im_unavailable(void *data, struct zwp_input_method_v2 *im) {
    WaylandInput *self = data;
    g_warning("Another input method is already running; text fields won't use this keyboard");
    g_clear_pointer(&self->im, zwp_input_method_v2_destroy);
}

static const struct zwp_input_method_v2_listener im_listener = {
    im_activate, im_deactivate, im_surrounding_text, im_text_change_cause,
    im_content_type, im_done, im_unavailable,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    WaylandInput *self = data;
    if (!strcmp(interface, zwp_input_method_manager_v2_interface.name))
        self->im_manager = wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1);
    else if (!strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name))
        self->vk_manager = wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_global_remove,
};

OskInput *osk_wayland_input_new(void) {
    GdkDisplay *gdk_display = gdk_display_get_default();
    if (!GDK_IS_WAYLAND_DISPLAY(gdk_display)) return NULL;

    WaylandInput *self = g_new0(WaylandInput, 1);
    self->base.vt = &wayland_vtable;
    self->display = gdk_wayland_display_get_wl_display(gdk_display);

    struct wl_registry *registry = wl_display_get_registry(self->display);
    wl_registry_add_listener(registry, &registry_listener, self);
    wl_display_roundtrip(self->display);
    wl_registry_destroy(registry);

    if (!self->im_manager && !self->vk_manager) {
        g_free(self);
        return NULL;
    }

    struct wl_seat *seat = gdk_wayland_seat_get_wl_seat(gdk_display_get_default_seat(gdk_display));
    if (self->im_manager) {
        self->im = zwp_input_method_manager_v2_get_input_method(self->im_manager, seat);
        zwp_input_method_v2_add_listener(self->im, &im_listener, self);
    }
    if (self->vk_manager) {
        self->vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(self->vk_manager, seat);
        for (gsize i = 0; i < G_N_ELEMENTS(special_keysyms); i++)
            self->keysyms[self->n_keysyms++] = special_keysyms[i];
        for (int c = 0x20; c < 0x7f; c++)
            self->keysyms[self->n_keysyms++] = xkb_utf32_to_keysym(c);
        upload_keymap(self);
    }
    return &self->base;
}
