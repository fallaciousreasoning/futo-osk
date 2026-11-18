/*
 * Non-static entry points into FUTO's JNI glue. Each glue-*.cpp file
 * includes one of FUTO's org_futo_*.cpp files unchanged and forwards to
 * its static JNI functions.
 */
#pragma once

#include "jni.h"

namespace futoglue {

jlong proximity_new(JNIEnv *env, jint width, jint height, jint grid_w, jint grid_h,
        jint common_key_w, jint common_key_h, jintArray proximity_chars, jint key_count,
        jintArray xs, jintArray ys, jintArray ws, jintArray hs, jintArray codes);
void proximity_free(JNIEnv *env, jlong info);

jlong session_new(JNIEnv *env, jstring locale, jlong dict_size);
void session_init(JNIEnv *env, jlong session, jlong dict);
void session_free(JNIEnv *env, jlong session);

jlong dict_open(JNIEnv *env, jstring path, jlong size);
void dict_close(JNIEnv *env, jlong dict);
void dict_suggest(JNIEnv *env, jlong dict, jlong info, jlong session, jintArray xs,
        jintArray ys, jintArray times, jintArray pointer_ids, jintArray code_points,
        jint input_size, jintArray options, jobjectArray prev_words,
        jbooleanArray prev_is_bos, jint prev_count, jintArray out_count,
        jintArray out_code_points, jintArray out_scores, jintArray out_space_indices,
        jintArray out_types, jintArray out_autocommit, jfloatArray inout_weight);

void lm_init();
jlong lm_open(JNIEnv *env, jstring path);
void lm_close(JNIEnv *env, jlong lm);
void lm_suggest(JNIEnv *env, jlong lm, jlong info, jstring context, jstring partial,
        jint input_mode, jintArray xs, jintArray ys, jfloat autocorrect_threshold,
        jobjectArray banned, jobjectArray out_words, jfloatArray out_probs);

} // namespace futoglue
