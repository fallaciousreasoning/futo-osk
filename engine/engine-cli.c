/*
 * Exercises the engine on a synthetic QWERTY layout.
 *
 *   futo-engine-cli DICT MODEL|- CONTEXT WORD [type|swipe]
 *
 * "type" taps the centre of each key of WORD (typos and all); "swipe"
 * drags through them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "futo-engine.h"

#define KEY_W 100
#define KEY_H 150

static const char *rows[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static const int offsets[] = { 0, 50, 150 };

static FutoKey keys[32];
static int n_keys;

static FutoKey *find_key(char c) {
    for (int i = 0; i < n_keys; i++)
        if (keys[i].code == c) return &keys[i];
    return NULL;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void print(const FutoSuggestions *s, double ms) {
    printf("%.1f ms, autocorrect=%d\n", ms, s->autocorrect);
    for (int i = 0; i < s->n; i++)
        printf("  %2d %-20s %d\n", i, s->items[i].word, s->items[i].score);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s DICT MODEL|- CONTEXT WORD [type|swipe]\n", argv[0]);
        return 2;
    }
    const char *mode = argc > 5 ? argv[5] : "type";

    for (int r = 0; r < 3; r++)
        for (const char *c = rows[r]; *c; c++)
            keys[n_keys++] = (FutoKey){ *c, offsets[r] + (int)(c - rows[r]) * KEY_W, r * KEY_H, KEY_W, KEY_H };

    double t = now_ms();
    FutoEngine *e = futo_engine_new(argv[1], strcmp(argv[2], "-") ? argv[2] : NULL, "en");
    if (!e) {
        fprintf(stderr, "failed to open %s\n", argv[1]);
        return 1;
    }
    printf("loaded in %.0f ms (lm: %s)\n", now_ms() - t, futo_engine_has_lm(e) ? "yes" : "no");
    futo_engine_set_layout(e, 10 * KEY_W, 4 * KEY_H, keys, n_keys);

    const char *word = argv[4];
    int n = (int)strlen(word);
    int xs[512], ys[512], times[512], np = 0;
    FutoSuggestions out;

    if (!strcmp(mode, "swipe")) {
        for (int i = 0; i < n; i++) {
            FutoKey *k = find_key(word[i]);
            if (!k) continue;
            int x = k->x + k->width / 2, y = k->y + k->height / 2;
            if (np == 0) {
                xs[np] = x; ys[np] = y; times[np] = 0; np++;
                continue;
            }
            int px = xs[np - 1], py = ys[np - 1];
            for (int s = 1; s <= 8 && np < 512; s++, np++) {
                xs[np] = px + (x - px) * s / 8;
                ys[np] = py + (y - py) * s / 8;
                times[np] = times[np - 1] + 12;
            }
        }
        t = now_ms();
        futo_engine_gesture(e, argv[3], xs, ys, times, np, &out);
    } else {
        for (int i = 0; i < n; i++) {
            FutoKey *k = find_key(word[i] | 0x20);
            xs[i] = k ? k->x + k->width / 2 : -1;
            ys[i] = k ? k->y + k->height / 2 : -1;
        }
        t = now_ms();
        futo_engine_suggest(e, argv[3], word, xs, ys, n, &out);
    }
    print(&out, now_ms() - t);

    /* Second run shows the steady-state cost once the LM's context is cached. */
    t = now_ms();
    if (!strcmp(mode, "swipe"))
        futo_engine_gesture(e, argv[3], xs, ys, times, np, &out);
    else
        futo_engine_suggest(e, argv[3], word, xs, ys, n, &out);
    printf("again: %.1f ms\n", now_ms() - t);

    futo_engine_free(e);
    return 0;
}
