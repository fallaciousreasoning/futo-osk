#include "osk-predictor.h"

typedef struct {
    guint id;
    gboolean gesture;
    char *context;
    char *partial;
    GArray *xs, *ys, *times;
} Request;

typedef struct {
    int width, height;
    GArray *keys;
} Layout;

struct OskPredictor {
    GThread *thread;
    GMutex lock;
    GCond cond;
    gboolean quit;
    Request *request; /* newest unserved request */
    Layout *layout;   /* newest unapplied layout */
    guint next_id;

    char *dict_path, *lm_path;
    OskPredictorCallback callback;
    gpointer data;
};

typedef struct {
    OskPredictor *self;
    guint id;
    FutoSuggestions result;
} Delivery;

static void request_free(Request *r) {
    if (!r) return;
    g_free(r->context);
    g_free(r->partial);
    g_array_unref(r->xs);
    g_array_unref(r->ys);
    g_array_unref(r->times);
    g_free(r);
}

static void layout_free(Layout *l) {
    if (!l) return;
    g_array_unref(l->keys);
    g_free(l);
}

static gboolean deliver(gpointer data) {
    Delivery *d = data;
    d->self->callback(d->id, &d->result, d->self->data);
    return G_SOURCE_REMOVE;
}

static gpointer run(gpointer data) {
    OskPredictor *self = data;
    FutoEngine *engine = futo_engine_new(self->dict_path, self->lm_path, "en");
    if (!engine)
        g_warning("Could not load the dictionary %s; suggestions are off", self->dict_path);
    else if (self->lm_path && !futo_engine_has_lm(engine))
        g_warning("Could not load the language model %s", self->lm_path);

    g_mutex_lock(&self->lock);
    while (!self->quit) {
        if (!self->request && !self->layout) {
            g_cond_wait(&self->cond, &self->lock);
            continue;
        }
        Layout *layout = g_steal_pointer(&self->layout);
        Request *r = g_steal_pointer(&self->request);
        g_mutex_unlock(&self->lock);

        if (engine && layout)
            futo_engine_set_layout(engine, layout->width, layout->height,
                                   (FutoKey *)layout->keys->data, layout->keys->len);
        if (r) {
            Delivery *d = g_new0(Delivery, 1);
            d->self = self;
            d->id = r->id;
            if (!engine)
                ;
            else if (r->gesture)
                futo_engine_gesture(engine, r->context, (int *)r->xs->data, (int *)r->ys->data,
                                    (int *)r->times->data, r->xs->len, &d->result);
            else
                futo_engine_suggest(engine, r->context, r->partial, (int *)r->xs->data,
                                    (int *)r->ys->data, r->xs->len, &d->result);
            g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT, deliver, d, g_free);
        }
        layout_free(layout);
        request_free(r);
        g_mutex_lock(&self->lock);
    }
    g_mutex_unlock(&self->lock);
    futo_engine_free(engine);
    return NULL;
}

OskPredictor *osk_predictor_new(const char *dict_path, const char *lm_path,
                                OskPredictorCallback callback, gpointer data) {
    OskPredictor *self = g_new0(OskPredictor, 1);
    g_mutex_init(&self->lock);
    g_cond_init(&self->cond);
    self->dict_path = g_strdup(dict_path);
    self->lm_path = g_strdup(lm_path);
    self->callback = callback;
    self->data = data;
    self->thread = g_thread_new("futo-engine", run, self);
    return self;
}

void osk_predictor_free(OskPredictor *self) {
    g_mutex_lock(&self->lock);
    self->quit = TRUE;
    g_cond_signal(&self->cond);
    g_mutex_unlock(&self->lock);
    g_thread_join(self->thread);

    request_free(self->request);
    layout_free(self->layout);
    g_mutex_clear(&self->lock);
    g_cond_clear(&self->cond);
    g_free(self->dict_path);
    g_free(self->lm_path);
    g_free(self);
}

void osk_predictor_set_layout(OskPredictor *self, int width, int height, const FutoKey *keys,
                              int n_keys) {
    Layout *l = g_new0(Layout, 1);
    l->width = width;
    l->height = height;
    l->keys = g_array_new(FALSE, FALSE, sizeof(FutoKey));
    g_array_append_vals(l->keys, keys, n_keys);

    g_mutex_lock(&self->lock);
    layout_free(self->layout);
    self->layout = l;
    g_cond_signal(&self->cond);
    g_mutex_unlock(&self->lock);
}

static GArray *int_array(const int *v, int n) {
    GArray *a = g_array_sized_new(FALSE, TRUE, sizeof(int), n);
    if (v) g_array_append_vals(a, v, n);
    else g_array_set_size(a, n);
    return a;
}

static guint submit(OskPredictor *self, Request *r) {
    g_mutex_lock(&self->lock);
    r->id = ++self->next_id;
    request_free(self->request);
    self->request = r;
    g_cond_signal(&self->cond);
    g_mutex_unlock(&self->lock);
    return r->id;
}

guint osk_predictor_suggest(OskPredictor *self, const char *context, const char *partial,
                            const int *xs, const int *ys, int n) {
    Request *r = g_new0(Request, 1);
    r->context = g_strdup(context);
    r->partial = g_strdup(partial);
    r->xs = int_array(xs, n);
    r->ys = int_array(ys, n);
    r->times = int_array(NULL, 0);
    return submit(self, r);
}

guint osk_predictor_gesture(OskPredictor *self, const char *context, const int *xs,
                            const int *ys, const int *times, int n) {
    Request *r = g_new0(Request, 1);
    r->gesture = TRUE;
    r->context = g_strdup(context);
    r->xs = int_array(xs, n);
    r->ys = int_array(ys, n);
    r->times = int_array(times, n);
    return submit(self, r);
}
