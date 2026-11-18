/*
 * futo-osk: an on-screen keyboard for phosh using FUTO Keyboard's
 * prediction, autocorrect and swipe typing.
 */
#include <gtk/gtk.h>
#include <locale.h>

#include "config.h"
#include "osk-ime.h"
#include "osk-keyboard.h"

#ifdef HAVE_LAYER_SHELL
#include <gtk4-layer-shell.h>
#endif
#ifdef HAVE_FEEDBACK
#define LIBFEEDBACK_USE_UNSTABLE_API
#include <libfeedback.h>
#endif

#define OSK_BUS_NAME "sm.puri.OSK0"
#define OSK_OBJECT_PATH "/sm/puri/OSK0"

static const char osk_introspection[] =
    "<node>"
    "  <interface name='sm.puri.OSK0'>"
    "    <method name='SetVisible'><arg type='b' name='visible' direction='in'/></method>"
    "    <property name='Visible' type='b' access='read'/>"
    "  </interface>"
    "</node>";

typedef struct {
    GtkApplication *app;
    GtkWindow *window;
    OskKeyboard *keyboard;
    OskIme *ime;
    GDBusConnection *bus;
    guint object_id;
    GSettings *a11y;
    GSettings *interface;
    gboolean visible;
    gboolean test;
} Osk;

static gboolean opt_test;
static int opt_row_height;
static char *opt_data_dir;

static const GOptionEntry options[] = {
    { "test", 't', 0, G_OPTION_ARG_NONE, &opt_test,
      "Show a text field and type into it instead of other apps", NULL },
    { "row-height", 'r', 0, G_OPTION_ARG_INT, &opt_row_height, "Key row height in pixels", "PX" },
    { "data-dir", 'd', 0, G_OPTION_ARG_FILENAME, &opt_data_dir,
      "Directory holding en/main.dict and en/model.gguf", "DIR" },
    { NULL },
};

static GSettings *settings_if_installed(const char *schema_id) {
    GSettingsSchemaSource *src = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = src ? g_settings_schema_source_lookup(src, schema_id, TRUE) : NULL;
    return schema ? g_settings_new(schema_id) : NULL;
}

/* --- Visibility --- */

static void emit_visible(Osk *osk) {
    if (!osk->bus || !osk->object_id) return;
    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", "Visible", g_variant_new_boolean(osk->visible));
    g_dbus_connection_emit_signal(osk->bus, NULL, OSK_OBJECT_PATH, "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}as)", OSK_BUS_NAME, &changed, NULL), NULL);
}

static void set_visible(Osk *osk, gboolean visible) {
    if (osk->test) visible = TRUE;
    if (osk->visible == visible) return;
    osk->visible = visible;
    gtk_widget_set_visible(GTK_WIDGET(osk->window), visible);
    emit_visible(osk);
}

static void on_focus(gboolean focused, gpointer data) {
    Osk *osk = data;
    gboolean auto_show = !osk->a11y || g_settings_get_boolean(osk->a11y, "screen-keyboard-enabled");
    if (focused && auto_show) set_visible(osk, TRUE);
    else if (!focused) set_visible(osk, FALSE);
}

static void on_method(GDBusConnection *bus, const char *sender, const char *path,
                      const char *iface, const char *method, GVariant *params,
                      GDBusMethodInvocation *invocation, gpointer data) {
    Osk *osk = data;
    if (!g_strcmp0(method, "SetVisible")) {
        gboolean visible;
        g_variant_get(params, "(b)", &visible);
        set_visible(osk, visible);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
}

static GVariant *on_get_property(GDBusConnection *bus, const char *sender, const char *path,
                                 const char *iface, const char *prop, GError **error, gpointer data) {
    Osk *osk = data;
    return g_variant_new_boolean(osk->visible);
}

static const GDBusInterfaceVTable osk_vtable = { .method_call = on_method, .get_property = on_get_property };

static void on_bus_acquired(GDBusConnection *bus, const char *name, gpointer data) {
    Osk *osk = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(osk_introspection, NULL);
    osk->bus = g_object_ref(bus);
    osk->object_id = g_dbus_connection_register_object(bus, OSK_OBJECT_PATH, info->interfaces[0],
                                                       &osk_vtable, osk, NULL, &error);
    if (!osk->object_id) g_warning("Could not export %s: %s", OSK_OBJECT_PATH, error->message);
}

static void on_name_lost(GDBusConnection *bus, const char *name, gpointer data) {
    g_warning("Could not own %s; is another on-screen keyboard running?", name);
}

/* --- Appearance --- */

static gboolean prefers_dark(Osk *osk) {
    if (!osk->interface) return TRUE;
    g_autofree char *scheme = g_settings_get_string(osk->interface, "color-scheme");
    return g_strcmp0(scheme, "prefer-light") != 0;
}

static void load_css(Osk *osk) {
    gboolean dark = prefers_dark(osk);
    osk_keyboard_set_dark(osk->keyboard, dark);
    g_autofree char *css = g_strdup_printf(
        "window.futo-osk { background: %1$s; }"
        ".futo-osk .bar { min-height: 42px; }"
        ".futo-osk .suggestions button { color: %2$s; font-size: 16px; padding: 0 6px;"
        "  border-radius: 8px; margin: 3px 2px; background: none; box-shadow: none; }"
        ".futo-osk .suggestions button:active { background: %3$s; }"
        ".futo-osk .suggestions button.autocorrect { font-weight: bold; }"
        ".futo-osk .suggestions button.centre { border-left: 1px solid %4$s;"
        "  border-right: 1px solid %4$s; border-radius: 0; margin: 9px 0; padding: 0 8px; }"
        ".futo-osk .hide { color: %2$s; background: none; box-shadow: none; min-width: 42px; }"
        ".futo-osk textview, .futo-osk textview text { background: %3$s; color: %2$s; }",
        dark ? "#121316" : "#EBECF0", dark ? "#E6E6EE" : "#191B21",
        dark ? "#3C3F47" : "#D4D5D9", dark ? "#2C2F38" : "#BCC2D6");
    static GtkCssProvider *provider;
    if (!provider) {
        provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    gtk_css_provider_load_from_string(provider, css);
}

static void on_color_scheme(GSettings *s, const char *key, Osk *osk) { load_css(osk); }

/* Row height follows the screen: square-ish keys in portrait, shorter in landscape. */
static void update_size(Osk *osk) {
    if (opt_row_height > 0) {
        osk_keyboard_set_row_height(osk->keyboard, opt_row_height);
        return;
    }
    GdkDisplay *display = gdk_display_get_default();
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(osk->window));
    GdkMonitor *monitor = surface ? gdk_display_get_monitor_at_surface(display, surface) : NULL;
    if (!monitor) monitor = g_list_model_get_item(gdk_display_get_monitors(display), 0);
    else g_object_ref(monitor);
    if (!monitor) return;

    GdkRectangle geo;
    gdk_monitor_get_geometry(monitor, &geo);
    int row = geo.width / 10;
    if (geo.width > geo.height) row = geo.height * 0.4 / 4;
    osk_keyboard_set_row_height(osk->keyboard, CLAMP(row, 36, 60));
    g_object_unref(monitor);
}

static void on_monitors_changed(GListModel *monitors, guint pos, guint removed, guint added, Osk *osk) {
    update_size(osk);
}

/* --- Setup --- */

static char *find_data(const char *name) {
    const char *dirs[] = { opt_data_dir, g_getenv("FUTO_OSK_DATA_DIR"), NULL, PKGDATADIR };
    g_autofree char *user = g_build_filename(g_get_user_data_dir(), "futo-osk", NULL);
    dirs[2] = user;
    for (gsize i = 0; i < G_N_ELEMENTS(dirs); i++) {
        if (!dirs[i]) continue;
        char *path = g_build_filename(dirs[i], "en", name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return path;
        g_free(path);
    }
    return NULL;
}

static void on_hide_clicked(GtkButton *button, Osk *osk) { set_visible(osk, FALSE); }

static void on_pressed(OskKeyboard *keyboard, Osk *osk) {
#ifdef HAVE_FEEDBACK
    /* Haptics and sound per the user's feedbackd profile, as stevia does. */
    if (!lfb_is_initted()) return;
    g_autoptr(LfbEvent) event = lfb_event_new("button-pressed");
    lfb_event_trigger_feedback_async(event, NULL, NULL, NULL);
#endif
}

static void activate(GtkApplication *app, Osk *osk) {
    if (osk->window) return;
    osk->test = opt_test;
    osk->a11y = settings_if_installed("org.gnome.desktop.a11y.applications");
    osk->interface = settings_if_installed("org.gnome.desktop.interface");

    osk->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(osk->window, "FUTO OSK");
    gtk_widget_add_css_class(GTK_WIDGET(osk->window), "futo-osk");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(osk->window, box);

    GtkWidget *text_view = NULL;
    if (osk->test) {
        text_view = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 8);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), 8);
        gtk_widget_set_size_request(text_view, -1, 120);
        gtk_box_append(GTK_BOX(box), text_view);
        gtk_window_set_default_size(osk->window, 540, -1);
    }

    osk->keyboard = OSK_KEYBOARD(osk_keyboard_new());
    gtk_widget_set_focusable(GTK_WIDGET(osk->keyboard), FALSE);
    g_signal_connect(osk->keyboard, "pressed", G_CALLBACK(on_pressed), osk);

    OskInput *input = osk->test ? osk_test_input_new(text_view) : osk_wayland_input_new();
    if (!input) {
        g_printerr("The compositor doesn't support input-method-v2 or virtual-keyboard-v1; "
                   "try --test\n");
        g_application_quit(G_APPLICATION(app));
        return;
    }

    g_autofree char *dict = find_data("main.dict");
    g_autofree char *model = find_data("model.gguf");
    if (!dict) g_warning("No en/main.dict found; suggestions are off");
    else if (!model) g_message("No en/model.gguf found; using the dictionary alone");
    osk->ime = osk_ime_new(input, osk->keyboard, dict, model);
    osk_ime_set_focus_callback(osk->ime, on_focus, osk);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(bar, "bar");
    gtk_box_append(GTK_BOX(bar), osk_ime_get_bar(osk->ime));
    GtkWidget *hide = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_add_css_class(hide, "hide");
    gtk_widget_set_focusable(hide, FALSE);
    gtk_widget_set_tooltip_text(hide, "Hide keyboard");
    g_signal_connect(hide, "clicked", G_CALLBACK(on_hide_clicked), osk);
    if (!osk->test) gtk_box_append(GTK_BOX(bar), hide);
    gtk_box_append(GTK_BOX(box), bar);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(osk->keyboard));

#ifdef HAVE_LAYER_SHELL
    if (!osk->test && gtk_layer_is_supported()) {
        gtk_layer_init_for_window(osk->window);
        /* phoc treats layer surfaces in the "osk" namespace as the on-screen keyboard. */
        gtk_layer_set_namespace(osk->window, "osk");
        gtk_layer_set_layer(osk->window, GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_anchor(osk->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_anchor(osk->window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(osk->window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_keyboard_mode(osk->window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_auto_exclusive_zone_enable(osk->window);
    }
#endif

    load_css(osk);
    if (osk->interface) g_signal_connect(osk->interface, "changed::color-scheme", G_CALLBACK(on_color_scheme), osk);
    update_size(osk);
    g_signal_connect(gdk_display_get_monitors(gdk_display_get_default()), "items-changed",
                     G_CALLBACK(on_monitors_changed), osk);

#ifdef HAVE_FEEDBACK
    g_autoptr(GError) error = NULL;
    if (!osk->test && !lfb_init("sm.puri.OSK0", &error)) g_message("No haptic feedback: %s", error->message);
#endif

    g_bus_own_name(G_BUS_TYPE_SESSION, OSK_BUS_NAME,
                   G_BUS_NAME_OWNER_FLAGS_REPLACE | G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT, on_bus_acquired,
                   NULL, on_name_lost, osk, NULL);

    if (osk->test) set_visible(osk, TRUE);
    else g_application_hold(G_APPLICATION(app)); /* stay alive while hidden */
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    Osk osk = { 0 };
    osk.app = gtk_application_new(NULL, G_APPLICATION_NON_UNIQUE);
    g_application_add_main_option_entries(G_APPLICATION(osk.app), options);
    g_signal_connect(osk.app, "activate", G_CALLBACK(activate), &osk);
    int status = g_application_run(G_APPLICATION(osk.app), argc, argv);
    g_clear_pointer(&osk.ime, osk_ime_free);
    g_object_unref(osk.app);
    return status;
}
