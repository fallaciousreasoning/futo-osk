/*
 * Drives FUTO's native engine the way FUTO Keyboard's Kotlin side does
 * (BinaryDictionary.java, LanguageModel.kt, LanguageModelFacilitator.kt),
 * simplified to one locale and no user dictionary.
 */
#include "futo-engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "fake-jni.h"
#include "glue.h"

namespace {

/* Native limits, from FUTO's native/jni/src/defines.h. */
constexpr int kMaxWordLength = 48;
constexpr int kMaxResults = 40;
constexpr int kMaxPrevWords = 3;
constexpr int kMaxProximityChars = 16;
constexpr int kMaxKeys = 64;
constexpr int kNotACode = -1;
constexpr int kLmMaxResults = 128;

/* ProximityInfo.java / config_keyboard_grid_{width,height} */
constexpr int kGridWidth = 32;
constexpr int kGridHeight = 16;
constexpr float kSearchDistance = 1.2f;

/* FUTO's setting defaults (LanguageModelFacilitator.kt). */
constexpr float kAutocorrectThreshold = 4.0f;
constexpr float kTransformerWeight = 3.4f;

/* SuggestOptions indices */
enum { kIsGesture, kUseFullEditDistance, kBlockOffensive, kSpaceAwareGesture, kLocaleWeight, kNumOptions };

using Candidates = std::vector<std::pair<std::string, long>>;

std::vector<int> to_code_points(const std::string &s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        int cp, n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 0xe) { cp = c & 0x0f; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++)
            cp = (cp << 6) | (s[i + k] & 0x3f);
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string to_utf8(const int *cps, int n) {
    std::string out;
    for (int i = 0; i < n; i++) {
        unsigned cp = cps[i];
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xc0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out += (char)(0xe0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3f));
            out += (char)(0x80 | (cp & 0x3f));
        } else {
            out += (char)(0xf0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3f));
            out += (char)(0x80 | ((cp >> 6) & 0x3f));
            out += (char)(0x80 | (cp & 0x3f));
        }
    }
    return out;
}

std::string lower_ascii(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return s;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\n");
    return s.substr(a, b - a + 1);
}

/* Applies the capitalization the user typed to a lowercase dictionary word. */
std::string match_caps(const std::string &word, const std::string &typed) {
    if (typed.empty() || !(typed[0] >= 'A' && typed[0] <= 'Z')) return word;
    bool all = typed.size() > 1 &&
               std::none_of(typed.begin(), typed.end(), [](char c) { return c >= 'a' && c <= 'z'; });
    std::string out = word;
    for (size_t i = 0; i < out.size(); i++) {
        if (i > 0 && !all) break;
        if (out[i] >= 'a' && out[i] <= 'z') out[i] = out[i] - 'a' + 'A';
    }
    return out;
}

/* LanguageModel.kt safeguardContext(), minus the cross-call matching. */
std::string safeguard_context(std::string ctx) {
    size_t nl = ctx.rfind('\n');
    if (nl != std::string::npos) ctx = ctx.substr(nl + 1);
    ctx = trim(ctx);

    auto needs_trim = [](const std::string &c) {
        return c.size() > 70 || std::count(c.begin(), c.end(), ' ') > 16;
    };
    if (needs_trim(ctx)) {
        size_t p = ctx.find_last_of(".?!");
        if (p != std::string::npos) ctx = trim(ctx.substr(p + 1));
    }
    while (needs_trim(ctx) && ctx.find(',') != std::string::npos)
        ctx = trim(ctx.substr(ctx.find(',') + 1));
    if (needs_trim(ctx)) {
        size_t p = ctx.size();
        for (int words = 0; words < 5 && p != std::string::npos && p > 0; words++)
            p = ctx.rfind(' ', p - 1);
        if (p != std::string::npos) ctx = ctx.substr(p + 1);
    }
    if (ctx.size() > 144) ctx = "";
    return ctx;
}

bool is_word_char(int cp) {
    return cp == '\'' || cp == '-' || cp >= 0x80 || (cp >= '0' && cp <= '9') ||
           (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
}

/* The word before the cursor, for the dictionary's bigram lookup. */
std::vector<int> previous_word(const std::string &context, bool *beginning_of_sentence) {
    std::vector<int> cps = to_code_points(context);
    int end = (int)cps.size();
    while (end > 0 && cps[end - 1] == ' ') end--;
    if (end == 0 || cps[end - 1] == '.' || cps[end - 1] == '!' || cps[end - 1] == '?' ||
        cps[end - 1] == '\n') {
        *beginning_of_sentence = true;
        return {};
    }
    *beginning_of_sentence = false;
    int start = end;
    while (start > 0 && is_word_char(cps[start - 1])) start--;
    if (end - start > kMaxWordLength) return {};
    return std::vector<int>(cps.begin() + start, cps.begin() + end);
}

} // namespace

struct FutoEngine {
    fakejni::Env env;
    jlong dict = 0;
    jlong session = 0;
    jlong info = 0;
    jlong lm = 0;
    long dict_size = 0;

    Candidates query_dict(const std::string &context, const std::vector<int> &code_points,
                          const int *xs, const int *ys, const int *times, int n, bool gesture);
    Candidates query_lm(const std::string &context, const std::string &partial, const int *xs,
                        const int *ys, int n, std::string *mode);
};

Candidates FutoEngine::query_dict(const std::string &context, const std::vector<int> &code_points,
                                  const int *xs, const int *ys, const int *times, int n,
                                  bool gesture) {
    Candidates out;
    if (!dict || !info) return out;
    JNIEnv *jni = env.jni();

    std::vector<int> cps(kMaxWordLength, kNotACode);
    if (!gesture) {
        n = std::min<int>(code_points.size(), kMaxWordLength - 1);
        std::copy_n(code_points.begin(), n, cps.begin());
    }
    std::vector<int> pointer_ids(n, 0), no_times(n, 0);

    int options[kNumOptions] = {};
    options[kIsGesture] = gesture;
    options[kLocaleWeight] = 1000;

    bool bos;
    std::vector<int> prev = previous_word(context, &bos);
    jobjectArray prev_words = env.objs(kMaxPrevWords);
    jbooleanArray prev_bos = env.bools(kMaxPrevWords);
    fakejni::objs(prev_words).v[0] = env.ints(prev.size(), prev.data());
    static_cast<fakejni::BoolArr *>(reinterpret_cast<fakejni::Obj *>(prev_bos))->v[0] = bos;

    float weight = -1.0f; /* NOT_A_WEIGHT_OF_LANG_MODEL_VS_SPATIAL_MODEL */
    jintArray out_count = env.ints(1);
    jintArray out_cps = env.ints(kMaxWordLength * kMaxResults);
    jintArray out_scores = env.ints(kMaxResults);

    futoglue::session_init(jni, session, dict);
    futoglue::dict_suggest(jni, dict, info, session, env.ints(n, xs), env.ints(n, ys),
                           env.ints(n, times ? times : no_times.data()),
                           env.ints(n, pointer_ids.data()), env.ints(kMaxWordLength, cps.data()),
                           n, env.ints(kNumOptions, options), prev_words, prev_bos,
                           prev.empty() && !bos ? 0 : 1, out_count, out_cps, out_scores,
                           env.ints(kMaxResults), env.ints(kMaxResults), env.ints(1),
                           env.floats(1, &weight));

    int count = fakejni::ints(out_count).v[0];
    const int *words = fakejni::ints(out_cps).v.data();
    for (int i = 0; i < count; i++) {
        const int *w = words + i * kMaxWordLength;
        int len = 0;
        while (len < kMaxWordLength && w[len] != 0) len++;
        if (len > 0) out.emplace_back(to_utf8(w, len), fakejni::ints(out_scores).v[i]);
    }
    env.clear();
    /* Native output is unordered; the Java side sorts it. */
    std::stable_sort(out.begin(), out.end(),
                     [](const auto &a, const auto &b) { return a.second > b.second; });
    return out;
}

Candidates FutoEngine::query_lm(const std::string &context, const std::string &partial,
                                const int *xs, const int *ys, int n, std::string *mode) {
    Candidates out;
    if (!lm || !info) return out;
    JNIEnv *jni = env.jni();

    n = std::min(n, 40);
    jobjectArray words = env.objs(kLmMaxResults);
    jfloatArray probs = env.floats(kLmMaxResults);
    futoglue::lm_suggest(jni, lm, info, env.str(safeguard_context(context)),
                         env.str(partial.substr(0, 40)), 0, env.ints(n, xs), env.ints(n, ys),
                         kAutocorrectThreshold, env.objs(0), words, probs);

    auto &w = fakejni::objs(words).v;
    auto &p = fakejni::floats(probs).v;
    const std::string *m = fakejni::str(w[kLmMaxResults - 1]);
    *mode = m ? *m : "clueless";

    /* LanguageModel.kt getSuggestionsInternal() + Facilitator reweighting */
    float mult = 500000.0f, offset = 100000.0f;
    if (*mode == "clueless") {
        mult = 10.0f;
        offset = -100000.0f;
    }
    int total = 0;
    for (int i = 0; i < kLmMaxResults - 1; i++)
        if (w[i]) total++;
    for (int i = 0, rank = 0; i < kLmMaxResults - 1; i++) {
        const std::string *s = fakejni::str(w[i]);
        if (!s) continue;
        long score = (long)((p[i] * mult + offset) * kTransformerWeight) - rank + (total - 1);
        out.emplace_back(trim(*s), score);
        rank++;
    }
    env.clear();
    return out;
}

static void emit(const Candidates &list, const std::string &autocorrect, FutoSuggestions *out) {
    out->n = 0;
    out->autocorrect = -1;
    std::vector<std::string> seen;
    for (const auto &c : list) {
        if (out->n >= (int)(sizeof(out->items) / sizeof(out->items[0]))) break;
        if (c.first.empty() || c.first.size() >= FUTO_MAX_WORD_BYTES) continue;
        if (std::find(seen.begin(), seen.end(), c.first) != seen.end()) continue;
        seen.push_back(c.first);
        FutoSuggestion *s = &out->items[out->n];
        strcpy(s->word, c.first.c_str());
        s->score = (int)std::clamp<long>(c.second, -2147483647L, 2147483647L);
        if (!autocorrect.empty() && c.first == autocorrect) out->autocorrect = out->n;
        out->n++;
    }
}

extern "C" {

FutoEngine *futo_engine_new(const char *dict_path, const char *lm_path, const char *locale) {
    static bool initialized = false;
    if (!initialized) {
        futoglue::lm_init();
        initialized = true;
    }

    struct stat st;
    if (stat(dict_path, &st) != 0) return nullptr;

    auto *e = new FutoEngine();
    JNIEnv *jni = e->env.jni();
    e->dict_size = st.st_size;
    e->dict = futoglue::dict_open(jni, e->env.str(dict_path), st.st_size);
    if (!e->dict) {
        delete e;
        return nullptr;
    }
    e->session = futoglue::session_new(jni, e->env.str(locale ? locale : "en"), e->dict_size);
    if (lm_path) e->lm = futoglue::lm_open(jni, e->env.str(lm_path));
    e->env.clear();
    return e;
}

void futo_engine_free(FutoEngine *e) {
    if (!e) return;
    JNIEnv *jni = e->env.jni();
    if (e->lm) futoglue::lm_close(jni, e->lm);
    if (e->info) futoglue::proximity_free(jni, e->info);
    if (e->session) futoglue::session_free(jni, e->session);
    if (e->dict) futoglue::dict_close(jni, e->dict);
    delete e;
}

int futo_engine_has_lm(const FutoEngine *e) { return e->lm != 0; }

/* ProximityInfo.java computeNearestNeighbors(), without its cell-skipping optimisation. */
void futo_engine_set_layout(FutoEngine *e, int width, int height, const FutoKey *keys, int n_keys) {
    JNIEnv *jni = e->env.jni();
    if (e->info) futoglue::proximity_free(jni, e->info);
    e->info = 0;

    std::vector<FutoKey> ks;
    for (int i = 0; i < n_keys && (int)ks.size() < kMaxKeys; i++)
        if (keys[i].code >= ' ') ks.push_back(keys[i]);
    if (ks.empty() || width <= 0 || height <= 0) return;

    /* The most common key width and height. */
    auto mode_of = [&](auto get) {
        int best = 0, best_n = 0;
        for (const auto &a : ks) {
            int c = (int)std::count_if(ks.begin(), ks.end(), [&](const FutoKey &b) { return get(b) == get(a); });
            if (c > best_n) { best = get(a); best_n = c; }
        }
        return best;
    };
    int common_w = mode_of([](const FutoKey &k) { return k.width; });
    int common_h = mode_of([](const FutoKey &k) { return k.height; });

    int cell_w = (width + kGridWidth - 1) / kGridWidth;
    int cell_h = (height + kGridHeight - 1) / kGridHeight;
    long threshold = (long)(common_w * kSearchDistance);
    long threshold_sq = threshold * threshold;

    std::vector<int> proximity(kGridWidth * kGridHeight * kMaxProximityChars, kNotACode);
    for (int gy = 0; gy < kGridHeight; gy++) {
        for (int gx = 0; gx < kGridWidth; gx++) {
            int cx = gx * cell_w + cell_w / 2, cy = gy * cell_h + cell_h / 2;
            int *cell = &proximity[(gy * kGridWidth + gx) * kMaxProximityChars];
            int n = 0;
            for (const auto &k : ks) {
                long dx = cx < k.x ? k.x - cx : cx >= k.x + k.width ? cx - (k.x + k.width - 1) : 0;
                long dy = cy < k.y ? k.y - cy : cy >= k.y + k.height ? cy - (k.y + k.height - 1) : 0;
                if (dx * dx + dy * dy < threshold_sq && n < kMaxProximityChars) cell[n++] = k.code;
            }
        }
    }

    std::vector<int> xs, ys, ws, hs, codes;
    for (const auto &k : ks) {
        xs.push_back(k.x);
        ys.push_back(k.y);
        ws.push_back(k.width);
        hs.push_back(k.height);
        codes.push_back(k.code);
    }
    int n = (int)ks.size();
    auto &env = e->env;
    e->info = futoglue::proximity_new(jni, width, height, kGridWidth, kGridHeight, common_w,
                                      common_h, env.ints(proximity.size(), proximity.data()), n,
                                      env.ints(n, xs.data()), env.ints(n, ys.data()),
                                      env.ints(n, ws.data()), env.ints(n, hs.data()),
                                      env.ints(n, codes.data()));
    env.clear();
}

void futo_engine_suggest(FutoEngine *e, const char *context_c, const char *partial_c,
                         const int *xs, const int *ys, int n_taps, FutoSuggestions *out) {
    std::string context = context_c ? context_c : "";
    std::string partial = partial_c ? partial_c : "";
    std::vector<int> cps = to_code_points(partial);
    n_taps = std::min<int>(n_taps, cps.size());

    Candidates dict;
    for (auto &c : e->query_dict(context, cps, xs, ys, nullptr, n_taps, false))
        dict.emplace_back(match_caps(c.first, partial), c.second);

    std::string mode;
    Candidates lm = e->query_lm(context, partial, xs, ys, n_taps, &mode);

    /* LanguageModelFacilitator.processAndMergeSuggestions(), roughly. */
    std::string autocorrect;
    if (partial.size() > 1) {
        bool typed_is_word = std::any_of(dict.begin(), dict.end(), [&](const auto &c) {
            return lower_ascii(c.first) == lower_ascii(partial);
        });
        if (!lm.empty() && !dict.empty() && lower_ascii(lm[0].first) == lower_ascii(dict[0].first)) {
            autocorrect = dict[0].first;
            lm[0].second += dict[0].second;
        } else if (!lm.empty() && mode == "autocorrect") {
            autocorrect = lm[0].first;
        } else if (!e->lm && !dict.empty() && !typed_is_word) {
            autocorrect = dict[0].first;
        }
        if (lower_ascii(autocorrect) == lower_ascii(partial)) autocorrect.clear();
    }

    Candidates all = lm;
    all.insert(all.end(), dict.begin(), dict.end());
    std::stable_sort(all.begin(), all.end(),
                     [](const auto &a, const auto &b) { return a.second > b.second; });
    if (!autocorrect.empty()) {
        /* Keep the autocorrect target first. */
        auto it = std::find_if(all.begin(), all.end(), [&](const auto &c) { return c.first == autocorrect; });
        if (it != all.end()) std::rotate(all.begin(), it, it + 1);
    }
    emit(all, autocorrect, out);
}

void futo_engine_gesture(FutoEngine *e, const char *context_c, const int *xs, const int *ys,
                         const int *times, int n_points, FutoSuggestions *out) {
    std::string context = context_c ? context_c : "";
    /* Like FUTO's classic swipe path: dictionary decoding with its bigrams,
     * no transformer rescoring (FUTO never calls rescoreSuggestionsNative). */
    emit(e->query_dict(context, {}, xs, ys, times, n_points, true), "", out);
    if (out->n > 0) out->autocorrect = 0;
}

} // extern "C"
