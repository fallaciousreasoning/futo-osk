/*
 * The key area: lays out an OskLayout, draws it in FUTO Keyboard's style
 * and turns touches into key presses, long-press choices, swipes and
 * space-bar cursor drags.
 */
#include "osk-keyboard.h"

#include <math.h>

#define MAX_POINTERS 4
#define LONG_PRESS_MS 300
#define REPEAT_DELAY_MS 400
#define REPEAT_INTERVAL_MS 50
#define DOUBLE_TAP_MS 350
#define KEY_GAP_X 3.0
#define KEY_GAP_Y 4.0
#define KEY_RADIUS 6.0

typedef struct {
    double x, y;
    guint32 t;
} TrailPoint;

typedef struct {
    gboolean in_use;
    GdkEventSequence *sequence; /* NULL for the mouse */
    OskKey *key;
    double start_x, start_y, x, y;
    guint32 start_time;
    GArray *trail; /* TrailPoint */
    gboolean gesture, popup, space_drag, done, repeating;
    int popup_sel;
    double drag_x;
    guint long_press_id, repeat_id;
    OskKeyboard *self;
} Pointer;

typedef struct {
    GdkRGBA background, key, key_mod, press, text, hint, primary, on_primary, trail;
} Palette;

struct _OskKeyboard {
    GtkWidget parent;

    OskLayout *layouts[OSK_N_LAYERS];
    OskLayer layer;
    OskShift shift;
    gint64 last_shift_tap;
    int row_height;
    char *space_label;
    gboolean dark;
    Palette palette;

    Pointer pointers[MAX_POINTERS];
    guint trail_fade_id;
    GArray *fading_trail;
    double fade;
};

G_DEFINE_FINAL_TYPE(OskKeyboard, osk_keyboard, GTK_TYPE_WIDGET)

enum { SIGNAL_PRESSED, SIGNAL_KEY, SIGNAL_GESTURE, SIGNAL_CURSOR, SIGNAL_GEOMETRY_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

static void rgba(GdkRGBA *c, guint32 hex) {
    c->red = ((hex >> 16) & 0xff) / 255.0;
    c->green = ((hex >> 8) & 0xff) / 255.0;
    c->blue = (hex & 0xff) / 255.0;
    c->alpha = 1.0;
}

/* FUTO's DefaultDarkScheme / DefaultLightScheme */
static void load_palette(OskKeyboard *self) {
    Palette *p = &self->palette;
    if (self->dark) {
        rgba(&p->background, 0x121316);
        rgba(&p->key, 0x1E2024);
        rgba(&p->key_mod, 0x17181C);
        rgba(&p->press, 0x3C3F47);
        rgba(&p->text, 0xE6E6EE);
        rgba(&p->hint, 0xBBBDBF);
        rgba(&p->primary, 0xB2C8FF);
        rgba(&p->on_primary, 0x131D36);
    } else {
        rgba(&p->background, 0xEBECF0);
        rgba(&p->key, 0xFFFFFF);
        rgba(&p->key_mod, 0xDCDDE2);
        rgba(&p->press, 0xD4D5D9);
        rgba(&p->text, 0x191B21);
        rgba(&p->hint, 0x484B54);
        rgba(&p->primary, 0x0A1633);
        rgba(&p->on_primary, 0xF2F6FF);
    }
    p->trail = p->primary;
}

static OskLayout *current(OskKeyboard *self) { return self->layouts[self->layer]; }

static gboolean shifted(OskKeyboard *self) { return self->shift != OSK_SHIFT_OFF; }

static double scale(OskKeyboard *self) { return gtk_widget_get_scale_factor(GTK_WIDGET(self)); }

/* --- Geometry --- */

static void lay_out(OskLayout *layout, double width, double row_h) {
    double unit = width / layout->units;
    for (int r = 0; r < layout->n_rows; r++) {
        OskRow *row = &layout->rows[r];
        double units = 0;
        for (int k = 0; k < row->n_keys; k++) units += row->keys[k].width;

        double x = (layout->units - units) / 2 * unit;
        double y = r * row_h;
        for (int k = 0; k < row->n_keys; k++) {
            OskKey *key = &row->keys[k];
            double w = key->width * unit;
            key->face = GRAPHENE_RECT_INIT(x + KEY_GAP_X, y + KEY_GAP_Y, w - 2 * KEY_GAP_X,
                                           row_h - 2 * KEY_GAP_Y);
            key->hit = GRAPHENE_RECT_INIT(x, y, w, row_h);
            /* Short rows' outer keys reach to the edges. */
            if (k == 0) {
                key->hit.size.width += key->hit.origin.x;
                key->hit.origin.x = 0;
            }
            if (k == row->n_keys - 1) key->hit.size.width = width - key->hit.origin.x;
            x += w;
        }
    }
}

static OskKey *key_at(OskKeyboard *self, double x, double y) {
    OskLayout *layout = current(self);
    double row_h = (double)gtk_widget_get_height(GTK_WIDGET(self)) / layout->n_rows;
    int r = CLAMP((int)(y / row_h), 0, layout->n_rows - 1);
    OskRow *row = &layout->rows[r];
    for (int k = 0; k < row->n_keys; k++) {
        graphene_rect_t *h = &row->keys[k].hit;
        if (x >= h->origin.x && x < h->origin.x + h->size.width) return &row->keys[k];
    }
    return x < 0 ? &row->keys[0] : &row->keys[row->n_keys - 1];
}

static double key_width(OskKeyboard *self) {
    return gtk_widget_get_width(GTK_WIDGET(self)) / current(self)->units;
}

int osk_keyboard_get_engine_keys(OskKeyboard *self, FutoKey *keys, int max_keys, int *width,
                                 int *height) {
    OskLayout *layout = self->layouts[OSK_LAYER_ALPHA];
    double s = scale(self);
    int n = 0;
    for (int r = 0; r < layout->n_rows; r++) {
        for (int k = 0; k < layout->rows[r].n_keys && n < max_keys; k++) {
            OskKey *key = &layout->rows[r].keys[k];
            if (key->type != OSK_KEY_CHAR) continue;
            /* The key's own slot, not its hit area: edge keys' hit areas
             * stretch to the screen edge, which would skew their centres. */
            gunichar c = g_utf8_get_char(key->text);
            keys[n++] = (FutoKey){ (int)c, (int)((key->face.origin.x - KEY_GAP_X) * s),
                                   (int)((key->face.origin.y - KEY_GAP_Y) * s),
                                   (int)((key->face.size.width + 2 * KEY_GAP_X) * s),
                                   (int)((key->face.size.height + 2 * KEY_GAP_Y) * s) };
        }
    }
    *width = gtk_widget_get_width(GTK_WIDGET(self)) * s;
    *height = gtk_widget_get_height(GTK_WIDGET(self)) * s;
    return n;
}

/* --- Long-press popup --- */

static int popup_count(Pointer *p) {
    return p->key->more ? (int)g_strv_length(p->key->more) : 0;
}

/* The first choice sits right above the key, so a plain long-press picks
 * it; the rest run towards the middle of the keyboard. */
static graphene_rect_t popup_cell(OskKeyboard *self, Pointer *p, int i) {
    double w = p->key->face.size.width + 2 * KEY_GAP_X;
    double h = p->key->face.size.height;
    double width = gtk_widget_get_width(GTK_WIDGET(self));
    int n = popup_count(p);
    double x0 = p->key->face.origin.x + p->key->face.size.width / 2 - w / 2;
    gboolean leftwards = x0 + w / 2 > width / 2;
    /* Shift the whole row back on screen if it runs off. */
    double lo = leftwards ? x0 - (n - 1) * w : x0;
    double shift = CLAMP(lo, 0, MAX(0, width - n * w)) - lo;
    double y = p->key->face.origin.y - h - KEY_GAP_Y * 2;
    return GRAPHENE_RECT_INIT(x0 + shift + (leftwards ? -i : i) * w, y, w, h);
}

static int popup_index_at(OskKeyboard *self, Pointer *p, double x) {
    int n = popup_count(p), best = 0;
    double best_d = G_MAXDOUBLE;
    for (int i = 0; i < n; i++) {
        graphene_rect_t c = popup_cell(self, p, i);
        double d = fabs(x - (c.origin.x + c.size.width / 2));
        if (d < best_d) best = i, best_d = d;
    }
    return best;
}

static char *popup_text(OskKeyboard *self, Pointer *p, int i) {
    const char *s = p->key->more[i];
    return shifted(self) ? g_utf8_strup(s, -1) : g_strdup(s);
}

/* --- Drawing --- */

static void fill_rounded(GtkSnapshot *snapshot, const graphene_rect_t *r, double radius,
                         const GdkRGBA *color) {
    GskRoundedRect rr;
    gsk_rounded_rect_init_from_rect(&rr, r, radius);
    gtk_snapshot_push_rounded_clip(snapshot, &rr);
    gtk_snapshot_append_color(snapshot, color, r);
    gtk_snapshot_pop(snapshot);
}

static void draw_text(OskKeyboard *self, GtkSnapshot *snapshot, const char *text, double cx,
                      double cy, double size, gboolean bold, const GdkRGBA *color) {
    PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), text);
    PangoFontDescription *font = pango_font_description_new();
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    if (bold) pango_font_description_set_weight(font, PANGO_WEIGHT_SEMIBOLD);
    pango_layout_set_font_description(layout, font);
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    gtk_snapshot_save(snapshot);
    gtk_snapshot_translate(snapshot, &GRAPHENE_POINT_INIT(round(cx - w / 2.0), round(cy - h / 2.0)));
    gtk_snapshot_append_layout(snapshot, layout, color);
    gtk_snapshot_restore(snapshot);
    pango_font_description_free(font);
    g_object_unref(layout);
}

static void draw_icon(OskKeyboard *self, GtkSnapshot *snapshot, OskKey *key, const GdkRGBA *color) {
    graphene_rect_t *f = &key->face;
    double cx = f->origin.x + f->size.width / 2, cy = f->origin.y + f->size.height / 2;
    double s = MIN(f->size.height, 40) * 0.22;
    cairo_t *cr = gtk_snapshot_append_cairo(snapshot, f);
    gdk_cairo_set_source_rgba(cr, color);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    switch (key->type) {
    case OSK_KEY_SHIFT:
        cairo_move_to(cr, cx, cy - s);
        cairo_line_to(cr, cx + s, cy);
        cairo_line_to(cr, cx + s * 0.45, cy);
        cairo_line_to(cr, cx + s * 0.45, cy + s * 0.8);
        cairo_line_to(cr, cx - s * 0.45, cy + s * 0.8);
        cairo_line_to(cr, cx - s * 0.45, cy);
        cairo_line_to(cr, cx - s, cy);
        cairo_close_path(cr);
        if (self->shift != OSK_SHIFT_OFF) cairo_fill_preserve(cr);
        cairo_stroke(cr);
        if (self->shift == OSK_SHIFT_LOCKED) {
            cairo_move_to(cr, cx - s * 0.45, cy + s * 1.25);
            cairo_line_to(cr, cx + s * 0.45, cy + s * 1.25);
            cairo_stroke(cr);
        }
        break;
    case OSK_KEY_DELETE:
        cairo_move_to(cr, cx - s * 1.3, cy);
        cairo_line_to(cr, cx - s * 0.6, cy - s * 0.8);
        cairo_line_to(cr, cx + s * 1.2, cy - s * 0.8);
        cairo_line_to(cr, cx + s * 1.2, cy + s * 0.8);
        cairo_line_to(cr, cx - s * 0.6, cy + s * 0.8);
        cairo_close_path(cr);
        cairo_move_to(cr, cx - s * 0.05, cy - s * 0.35);
        cairo_line_to(cr, cx + s * 0.65, cy + s * 0.35);
        cairo_move_to(cr, cx + s * 0.65, cy - s * 0.35);
        cairo_line_to(cr, cx - s * 0.05, cy + s * 0.35);
        cairo_stroke(cr);
        break;
    case OSK_KEY_ENTER:
        cairo_move_to(cr, cx + s, cy - s * 0.8);
        cairo_line_to(cr, cx + s, cy + s * 0.3);
        cairo_line_to(cr, cx - s, cy + s * 0.3);
        cairo_move_to(cr, cx - s * 0.5, cy - s * 0.2);
        cairo_line_to(cr, cx - s, cy + s * 0.3);
        cairo_line_to(cr, cx - s * 0.5, cy + s * 0.8);
        cairo_stroke(cr);
        break;
    default:
        break;
    }
    cairo_destroy(cr);
}

static gboolean key_is_down(OskKeyboard *self, OskKey *key) {
    for (int i = 0; i < MAX_POINTERS; i++) {
        Pointer *p = &self->pointers[i];
        if (p->in_use && p->key == key && !p->gesture && !p->done) return TRUE;
    }
    return FALSE;
}

static void draw_key(OskKeyboard *self, GtkSnapshot *snapshot, OskKey *key) {
    Palette *pal = &self->palette;
    const GdkRGBA *bg = key->type == OSK_KEY_CHAR || key->type == OSK_KEY_SPACE ? &pal->key : &pal->key_mod;
    const GdkRGBA *fg = &pal->text;
    if (key->type == OSK_KEY_ENTER) {
        bg = &pal->primary;
        fg = &pal->on_primary;
    }
    if (key_is_down(self, key)) bg = &pal->press;
    fill_rounded(snapshot, &key->face, KEY_RADIUS, bg);

    graphene_rect_t *f = &key->face;
    double cx = f->origin.x + f->size.width / 2, cy = f->origin.y + f->size.height / 2;
    const char *label = osk_key_label(key, shifted(self));
    if (label) {
        gboolean small = key->type != OSK_KEY_CHAR || g_utf8_strlen(label, -1) > 1;
        draw_text(self, snapshot, label, cx, cy, f->size.height * (small ? 0.32 : 0.46), small, fg);
    } else {
        draw_icon(self, snapshot, key, fg);
    }
    if (key->type == OSK_KEY_SPACE && self->space_label)
        draw_text(self, snapshot, self->space_label, cx, cy, f->size.height * 0.26, FALSE, &self->palette.hint);
    if (key->hint)
        draw_text(self, snapshot, key->hint, f->origin.x + f->size.width - 7, f->origin.y + 8,
                  f->size.height * 0.24, FALSE, &pal->hint);
}

static void draw_trail(OskKeyboard *self, GtkSnapshot *snapshot, GArray *trail, double alpha) {
    if (!trail || trail->len < 2) return;
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, gtk_widget_get_width(GTK_WIDGET(self)),
                                                gtk_widget_get_height(GTK_WIDGET(self)));
    cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &bounds);
    GdkRGBA c = self->palette.trail;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    /* Thin and faint at the tail, thick at the finger. */
    guint n = trail->len, from = n > 60 ? n - 60 : 0;
    for (guint i = from + 1; i < n; i++) {
        TrailPoint *a = &g_array_index(trail, TrailPoint, i - 1);
        TrailPoint *b = &g_array_index(trail, TrailPoint, i);
        double t = (double)(i - from) / (n - from);
        c.alpha = alpha * (0.25 + 0.75 * t);
        gdk_cairo_set_source_rgba(cr, &c);
        cairo_set_line_width(cr, 2 + 4 * t);
        cairo_move_to(cr, a->x, a->y);
        cairo_line_to(cr, b->x, b->y);
        cairo_stroke(cr);
    }
    cairo_destroy(cr);
}

static void draw_bubble(OskKeyboard *self, GtkSnapshot *snapshot, Pointer *p) {
    Palette *pal = &self->palette;
    if (p->popup) {
        int n = popup_count(p);
        graphene_rect_t first = popup_cell(self, p, 0), last = popup_cell(self, p, n - 1);
        graphene_rect_t all;
        graphene_rect_union(&first, &last, &all);
        fill_rounded(snapshot, &all, KEY_RADIUS + 2, &pal->press);
        for (int i = 0; i < n; i++) {
            graphene_rect_t c = popup_cell(self, p, i);
            if (i == p->popup_sel) {
                graphene_rect_t in = GRAPHENE_RECT_INIT(c.origin.x + 2, c.origin.y + 2,
                                                        c.size.width - 4, c.size.height - 4);
                fill_rounded(snapshot, &in, KEY_RADIUS, &pal->primary);
            }
            g_autofree char *t = popup_text(self, p, i);
            draw_text(self, snapshot, t, c.origin.x + c.size.width / 2, c.origin.y + c.size.height / 2,
                      c.size.height * 0.44, FALSE, i == p->popup_sel ? &pal->on_primary : &pal->text);
        }
        return;
    }
    /* Key preview above a pressed character key. */
    if (p->key->type != OSK_KEY_CHAR || p->gesture) return;
    graphene_rect_t *f = &p->key->face;
    graphene_rect_t b = GRAPHENE_RECT_INIT(f->origin.x - 4, f->origin.y - f->size.height - 10,
                                           f->size.width + 8, f->size.height + 4);
    fill_rounded(snapshot, &b, KEY_RADIUS + 2, &pal->press);
    draw_text(self, snapshot, osk_key_label(p->key, shifted(self)), b.origin.x + b.size.width / 2,
              b.origin.y + b.size.height / 2, f->size.height * 0.56, FALSE, &pal->text);
}

static void osk_keyboard_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
    OskKeyboard *self = OSK_KEYBOARD(widget);
    OskLayout *layout = current(self);
    gtk_snapshot_append_color(snapshot, &self->palette.background,
                              &GRAPHENE_RECT_INIT(0, 0, gtk_widget_get_width(widget),
                                                  gtk_widget_get_height(widget)));
    for (int r = 0; r < layout->n_rows; r++)
        for (int k = 0; k < layout->rows[r].n_keys; k++)
            draw_key(self, snapshot, &layout->rows[r].keys[k]);

    draw_trail(self, snapshot, self->fading_trail, self->fade);
    for (int i = 0; i < MAX_POINTERS; i++) {
        Pointer *p = &self->pointers[i];
        if (!p->in_use || p->done) continue;
        if (p->gesture) draw_trail(self, snapshot, p->trail, 1.0);
        else if (key_is_down(self, p->key)) draw_bubble(self, snapshot, p);
    }
}

/* --- Input --- */

static void emit_key(OskKeyboard *self, OskKey *key, const char *text, double x, double y) {
    double s = scale(self);
    g_signal_emit(self, signals[SIGNAL_KEY], 0, key, text, (int)(x * s), (int)(y * s));
}

static void set_layer(OskKeyboard *self, OskLayer layer) {
    if (self->layer == layer) return;
    self->layer = layer;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void press_shift(OskKeyboard *self) {
    gint64 now = g_get_monotonic_time();
    if (self->shift == OSK_SHIFT_LOCKED)
        self->shift = OSK_SHIFT_OFF;
    else if (self->shift == OSK_SHIFT_ONCE && now - self->last_shift_tap < DOUBLE_TAP_MS * 1000)
        self->shift = OSK_SHIFT_LOCKED;
    else if (self->shift == OSK_SHIFT_ONCE)
        self->shift = OSK_SHIFT_OFF;
    else
        self->shift = OSK_SHIFT_ONCE;
    self->last_shift_tap = now;
}

/* A plain press of the pointer's key. */
static void activate(OskKeyboard *self, Pointer *p) {
    OskKey *key = p->key;
    switch (key->type) {
    case OSK_KEY_SHIFT: press_shift(self); break;
    case OSK_KEY_SYMBOLS: set_layer(self, OSK_LAYER_SYMBOLS); break;
    case OSK_KEY_SYMBOLS2: set_layer(self, OSK_LAYER_SYMBOLS2); break;
    case OSK_KEY_ALPHA: set_layer(self, OSK_LAYER_ALPHA); break;
    case OSK_KEY_NUMBERS: set_layer(self, OSK_LAYER_NUMBERS); break;
    case OSK_KEY_DELETE:
        /* Typed on press, and repeated while held. */
        break;
    case OSK_KEY_CHAR:
        emit_key(self, key, osk_key_label(key, shifted(self)), p->x, p->y);
        if (self->shift == OSK_SHIFT_ONCE) self->shift = OSK_SHIFT_OFF;
        break;
    case OSK_KEY_SPACE:
        emit_key(self, key, NULL, p->x, p->y);
        /* Like AOSP, a space typed on a symbols layer goes back to letters. */
        if (self->layer == OSK_LAYER_SYMBOLS || self->layer == OSK_LAYER_SYMBOLS2)
            set_layer(self, OSK_LAYER_ALPHA);
        break;
    default:
        emit_key(self, key, NULL, p->x, p->y);
        break;
    }
}

static Pointer *find_pointer(OskKeyboard *self, GdkEventSequence *sequence) {
    for (int i = 0; i < MAX_POINTERS; i++)
        if (self->pointers[i].in_use && self->pointers[i].sequence == sequence)
            return &self->pointers[i];
    return NULL;
}

static void end_pointer(Pointer *p) {
    g_clear_handle_id(&p->long_press_id, g_source_remove);
    g_clear_handle_id(&p->repeat_id, g_source_remove);
    g_clear_pointer(&p->trail, g_array_unref);
    p->in_use = FALSE;
}

static gboolean on_repeat(gpointer data) {
    Pointer *p = data;
    emit_key(p->self, p->key, NULL, p->x, p->y);
    if (!p->repeating) {
        /* After the initial delay, repeat faster. */
        p->repeating = TRUE;
        p->repeat_id = g_timeout_add(REPEAT_INTERVAL_MS, on_repeat, p);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean on_long_press(gpointer data) {
    Pointer *p = data;
    p->long_press_id = 0;
    if (p->gesture || p->done || popup_count(p) == 0) return G_SOURCE_REMOVE;
    p->popup = TRUE;
    p->popup_sel = 0;
    gtk_widget_queue_draw(GTK_WIDGET(p->self));
    return G_SOURCE_REMOVE;
}

static gboolean can_swipe(OskKeyboard *self, Pointer *p) {
    if (!current(self)->words || p->key->type != OSK_KEY_CHAR) return FALSE;
    for (int i = 0; i < MAX_POINTERS; i++)
        if (&self->pointers[i] != p && self->pointers[i].in_use && !self->pointers[i].done) return FALSE;
    return g_unichar_isalpha(g_utf8_get_char(p->key->text));
}

static void trail_add(Pointer *p, double x, double y, guint32 t) {
    TrailPoint pt = { x, y, t };
    g_array_append_val(p->trail, pt);
}

static void pointer_down(OskKeyboard *self, GdkEventSequence *seq, double x, double y, guint32 t) {
    /* Typing fast overlaps touches: a new touch completes the previous key. */
    for (int i = 0; i < MAX_POINTERS; i++) {
        Pointer *o = &self->pointers[i];
        if (o->in_use && !o->done && !o->gesture && !o->popup && !o->space_drag &&
            o->key->type != OSK_KEY_DELETE) {
            g_clear_handle_id(&o->long_press_id, g_source_remove);
            activate(self, o);
            o->done = TRUE;
        }
    }

    Pointer *p = NULL;
    for (int i = 0; i < MAX_POINTERS && !p; i++)
        if (!self->pointers[i].in_use) p = &self->pointers[i];
    if (!p) return;

    *p = (Pointer){ .in_use = TRUE, .sequence = seq, .self = self };
    p->key = key_at(self, x, y);
    p->start_x = p->x = x;
    p->start_y = p->y = y;
    p->start_time = t;
    p->drag_x = x;
    p->trail = g_array_new(FALSE, FALSE, sizeof(TrailPoint));
    trail_add(p, x, y, t);
    g_signal_emit(self, signals[SIGNAL_PRESSED], 0);

    if (p->key->type == OSK_KEY_DELETE) {
        emit_key(self, p->key, NULL, x, y);
        p->repeat_id = g_timeout_add(REPEAT_DELAY_MS, on_repeat, p);
    } else if (p->key->more) {
        p->long_press_id = g_timeout_add(LONG_PRESS_MS, on_long_press, p);
    }
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void pointer_move(OskKeyboard *self, Pointer *p, double x, double y, guint32 t) {
    p->x = x;
    p->y = y;
    if (p->done) return;

    double kw = key_width(self);
    if (p->popup) {
        int sel = popup_index_at(self, p, x);
        if (sel != p->popup_sel) {
            p->popup_sel = sel;
            gtk_widget_queue_draw(GTK_WIDGET(self));
        }
        return;
    }

    if (p->key->type == OSK_KEY_SPACE) {
        if (!p->space_drag && fabs(x - p->start_x) > kw * 0.4) p->space_drag = TRUE;
        while (p->space_drag && fabs(x - p->drag_x) > kw * 0.35) {
            int dir = x > p->drag_x ? 1 : -1;
            p->drag_x += dir * kw * 0.35;
            g_signal_emit(self, signals[SIGNAL_CURSOR], 0, dir);
        }
        return;
    }

    TrailPoint *last = &g_array_index(p->trail, TrailPoint, p->trail->len - 1);
    if (hypot(x - last->x, y - last->y) >= 2) trail_add(p, x, y, t);

    if (!p->gesture && can_swipe(self, p) &&
        hypot(x - p->start_x, y - p->start_y) > kw * 0.75) {
        p->gesture = TRUE;
        g_clear_handle_id(&p->long_press_id, g_source_remove);
    }
    if (p->gesture) {
        gtk_widget_queue_draw(GTK_WIDGET(self));
        return;
    }

    /* Sliding onto another key retargets the press. */
    OskKey *under = key_at(self, x, y);
    if (under != p->key && p->key->type != OSK_KEY_DELETE &&
        !(p->key->type == OSK_KEY_CHAR && can_swipe(self, p))) {
        p->key = under;
        g_clear_handle_id(&p->long_press_id, g_source_remove);
        if (under->more) p->long_press_id = g_timeout_add(LONG_PRESS_MS, on_long_press, p);
        gtk_widget_queue_draw(GTK_WIDGET(self));
    }
}

static gboolean fade_trail(gpointer data) {
    OskKeyboard *self = data;
    self->fade -= 0.12;
    gtk_widget_queue_draw(GTK_WIDGET(self));
    if (self->fade > 0) return G_SOURCE_CONTINUE;
    g_clear_pointer(&self->fading_trail, g_array_unref);
    self->trail_fade_id = 0;
    return G_SOURCE_REMOVE;
}

static void pointer_up(OskKeyboard *self, Pointer *p, gboolean cancelled) {
    if (!p->done && !cancelled) {
        if (p->gesture) {
            double s = scale(self);
            GArray *points = g_array_sized_new(FALSE, FALSE, sizeof(OskGesturePoint), p->trail->len);
            for (guint i = 0; i < p->trail->len; i++) {
                TrailPoint *tp = &g_array_index(p->trail, TrailPoint, i);
                OskGesturePoint gp = { (int)(tp->x * s), (int)(tp->y * s), (int)(tp->t - p->start_time) };
                g_array_append_val(points, gp);
            }
            g_signal_emit(self, signals[SIGNAL_GESTURE], 0, points, shifted(self));
            g_array_unref(points);
            if (self->shift == OSK_SHIFT_ONCE) self->shift = OSK_SHIFT_OFF;

            g_clear_pointer(&self->fading_trail, g_array_unref);
            self->fading_trail = g_steal_pointer(&p->trail);
            self->fade = 1.0;
            g_clear_handle_id(&self->trail_fade_id, g_source_remove);
            self->trail_fade_id = g_timeout_add(16, fade_trail, self);
        } else if (p->popup) {
            g_autofree char *text = popup_text(self, p, p->popup_sel);
            emit_key(self, p->key, text, p->start_x, p->start_y);
            if (self->shift == OSK_SHIFT_ONCE) self->shift = OSK_SHIFT_OFF;
        } else if (!p->space_drag && p->key->type != OSK_KEY_DELETE) {
            activate(self, p);
        }
    }
    end_pointer(p);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static gboolean event_point(OskKeyboard *self, GdkEvent *event, double *x, double *y) {
    double ex, ey, nx, ny;
    if (!gdk_event_get_position(event, &ex, &ey)) return FALSE;
    GtkNative *native = gtk_widget_get_native(GTK_WIDGET(self));
    gtk_native_get_surface_transform(native, &nx, &ny);
    graphene_point_t in = GRAPHENE_POINT_INIT(ex - nx, ey - ny), out;
    if (!gtk_widget_compute_point(GTK_WIDGET(native), GTK_WIDGET(self), &in, &out)) return FALSE;
    *x = out.x;
    *y = out.y;
    return TRUE;
}

static gboolean on_event(GtkEventControllerLegacy *controller, GdkEvent *event, OskKeyboard *self) {
    GdkEventType type = gdk_event_get_event_type(event);
    GdkEventSequence *seq = gdk_event_get_event_sequence(event);
    guint32 t = gdk_event_get_time(event);
    double x, y;

    switch (type) {
    case GDK_BUTTON_PRESS:
        if (gdk_button_event_get_button(event) != GDK_BUTTON_PRIMARY) return FALSE;
        seq = NULL;
        G_GNUC_FALLTHROUGH;
    case GDK_TOUCH_BEGIN:
        if (event_point(self, event, &x, &y)) pointer_down(self, seq, x, y, t);
        return TRUE;
    case GDK_MOTION_NOTIFY:
        seq = NULL;
        G_GNUC_FALLTHROUGH;
    case GDK_TOUCH_UPDATE: {
        Pointer *p = find_pointer(self, seq);
        if (p && event_point(self, event, &x, &y)) pointer_move(self, p, x, y, t);
        return p != NULL;
    }
    case GDK_BUTTON_RELEASE:
        seq = NULL;
        G_GNUC_FALLTHROUGH;
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL: {
        Pointer *p = find_pointer(self, seq);
        if (!p) return FALSE;
        if (event_point(self, event, &x, &y)) pointer_move(self, p, x, y, t);
        pointer_up(self, p, type == GDK_TOUCH_CANCEL);
        return TRUE;
    }
    default:
        return FALSE;
    }
}

/* --- GtkWidget --- */

static void osk_keyboard_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
                                 int *min, int *nat, int *min_baseline, int *nat_baseline) {
    OskKeyboard *self = OSK_KEYBOARD(widget);
    if (orientation == GTK_ORIENTATION_VERTICAL) {
        *min = *nat = self->row_height * current(self)->n_rows;
    } else {
        *min = 200;
        *nat = 360;
    }
}

static void osk_keyboard_size_allocate(GtkWidget *widget, int width, int height, int baseline) {
    OskKeyboard *self = OSK_KEYBOARD(widget);
    for (int l = 0; l < OSK_N_LAYERS; l++)
        lay_out(self->layouts[l], width, (double)height / self->layouts[l]->n_rows);
    g_signal_emit(self, signals[SIGNAL_GEOMETRY_CHANGED], 0);
}

static void osk_keyboard_dispose(GObject *object) {
    OskKeyboard *self = OSK_KEYBOARD(object);
    for (int i = 0; i < MAX_POINTERS; i++)
        if (self->pointers[i].in_use) end_pointer(&self->pointers[i]);
    g_clear_handle_id(&self->trail_fade_id, g_source_remove);
    g_clear_pointer(&self->fading_trail, g_array_unref);
    for (int l = 0; l < OSK_N_LAYERS; l++) g_clear_pointer(&self->layouts[l], osk_layout_free);
    g_clear_pointer(&self->space_label, g_free);
    G_OBJECT_CLASS(osk_keyboard_parent_class)->dispose(object);
}

static void osk_keyboard_class_init(OskKeyboardClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    object_class->dispose = osk_keyboard_dispose;
    widget_class->snapshot = osk_keyboard_snapshot;
    widget_class->measure = osk_keyboard_measure;
    widget_class->size_allocate = osk_keyboard_size_allocate;

    signals[SIGNAL_PRESSED] = g_signal_new("pressed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                                           NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_KEY] = g_signal_new("key", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                       NULL, NULL, G_TYPE_NONE, 4, G_TYPE_POINTER, G_TYPE_STRING,
                                       G_TYPE_INT, G_TYPE_INT);
    signals[SIGNAL_GESTURE] = g_signal_new("gesture", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                                           NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_POINTER,
                                           G_TYPE_BOOLEAN);
    signals[SIGNAL_CURSOR] = g_signal_new("cursor", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                                          NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
    signals[SIGNAL_GEOMETRY_CHANGED] = g_signal_new("geometry-changed", G_TYPE_FROM_CLASS(klass),
                                                    G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                                    G_TYPE_NONE, 0);
}

static void osk_keyboard_init(OskKeyboard *self) {
    for (int l = 0; l < OSK_N_LAYERS; l++) self->layouts[l] = osk_layout_new(l);
    self->row_height = 54;
    self->dark = TRUE;
    load_palette(self);

    /* Draw long-press popups and previews over whatever sits above us. */
    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_VISIBLE);

    GtkEventController *legacy = gtk_event_controller_legacy_new();
    g_signal_connect(legacy, "event", G_CALLBACK(on_event), self);
    gtk_widget_add_controller(GTK_WIDGET(self), legacy);
}

GtkWidget *osk_keyboard_new(void) { return g_object_new(OSK_TYPE_KEYBOARD, NULL); }

void osk_keyboard_set_layer(OskKeyboard *self, OskLayer layer) { set_layer(self, layer); }

OskLayer osk_keyboard_get_layer(OskKeyboard *self) { return self->layer; }

void osk_keyboard_set_shift(OskKeyboard *self, OskShift shift) {
    if (self->shift == shift) return;
    self->shift = shift;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

OskShift osk_keyboard_get_shift(OskKeyboard *self) { return self->shift; }

void osk_keyboard_set_row_height(OskKeyboard *self, int height) {
    if (self->row_height == height) return;
    self->row_height = height;
    gtk_widget_queue_resize(GTK_WIDGET(self));
}

void osk_keyboard_set_space_label(OskKeyboard *self, const char *label) {
    g_free(self->space_label);
    self->space_label = g_strdup(label);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void osk_keyboard_set_dark(OskKeyboard *self, gboolean dark) {
    self->dark = dark;
    load_palette(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

OskKey *osk_keyboard_find_key(OskKeyboard *self, OskKeyType type, const char *text, int *x, int *y) {
    OskLayout *layout = current(self);
    double s = scale(self);
    for (int r = 0; r < layout->n_rows; r++) {
        for (int k = 0; k < layout->rows[r].n_keys; k++) {
            OskKey *key = &layout->rows[r].keys[k];
            if (key->type != type || (text && g_strcmp0(key->text, text))) continue;
            *x = (key->hit.origin.x + key->hit.size.width / 2) * s;
            *y = (key->hit.origin.y + key->hit.size.height / 2) * s;
            return key;
        }
    }
    return NULL;
}
