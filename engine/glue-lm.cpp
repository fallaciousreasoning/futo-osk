/* FUTO relies on Android's libc++ exporting isnan() globally. */
#include <math.h>

#include "org_futo_inputmethod_latin_xlm_LanguageModel.cpp"
#include "glue.h"

void futoglue::lm_init() {
    llama_backend_init(true);
    llama_log_set(latinime::llama_log_callback, nullptr);
}

jlong futoglue::lm_open(JNIEnv *env, jstring path) {
    return latinime::xlm_LanguageModel_open(env, nullptr, path);
}

void futoglue::lm_close(JNIEnv *env, jlong lm) {
    latinime::xlm_LanguageModel_close(env, nullptr, lm);
}

void futoglue::lm_suggest(JNIEnv *env, jlong lm, jlong info, jstring context, jstring partial,
        jint input_mode, jintArray xs, jintArray ys, jfloat autocorrect_threshold,
        jobjectArray banned, jobjectArray out_words, jfloatArray out_probs) {
    latinime::xlm_LanguageModel_getSuggestions(env, nullptr, lm, info, context, partial,
            input_mode, xs, ys, autocorrect_threshold, banned, out_words, out_probs);
}
