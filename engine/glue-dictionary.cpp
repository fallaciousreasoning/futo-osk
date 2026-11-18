#include "org_futo_inputmethod_latin_BinaryDictionary.cpp"
#include "glue.h"

jlong futoglue::dict_open(JNIEnv *env, jstring path, jlong size) {
    return latinime::latinime_BinaryDictionary_open(env, nullptr, path, 0, size, JNI_FALSE);
}

void futoglue::dict_close(JNIEnv *env, jlong dict) {
    latinime::latinime_BinaryDictionary_close(env, nullptr, dict);
}

void futoglue::dict_suggest(JNIEnv *env, jlong dict, jlong info, jlong session, jintArray xs,
        jintArray ys, jintArray times, jintArray pointer_ids, jintArray code_points,
        jint input_size, jintArray options, jobjectArray prev_words,
        jbooleanArray prev_is_bos, jint prev_count, jintArray out_count,
        jintArray out_code_points, jintArray out_scores, jintArray out_space_indices,
        jintArray out_types, jintArray out_autocommit, jfloatArray inout_weight) {
    latinime::latinime_BinaryDictionary_getSuggestions(env, nullptr, dict, info, session, xs, ys,
            times, pointer_ids, code_points, input_size, options, prev_words, prev_is_bos,
            prev_count, out_count, out_code_points, out_scores, out_space_indices, out_types,
            out_autocommit, inout_weight);
}
