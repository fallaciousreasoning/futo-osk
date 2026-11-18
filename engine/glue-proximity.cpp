#include "org_futo_inputmethod_keyboard_ProximityInfo.cpp"
#include "glue.h"

jlong futoglue::proximity_new(JNIEnv *env, jint width, jint height, jint grid_w, jint grid_h,
        jint common_key_w, jint common_key_h, jintArray proximity_chars, jint key_count,
        jintArray xs, jintArray ys, jintArray ws, jintArray hs, jintArray codes) {
    return latinime::latinime_Keyboard_setProximityInfo(env, nullptr, width, height, grid_w,
            grid_h, common_key_w, common_key_h, proximity_chars, key_count, xs, ys, ws, hs,
            codes, nullptr, nullptr, nullptr);
}

void futoglue::proximity_free(JNIEnv *env, jlong info) {
    latinime::latinime_Keyboard_release(env, nullptr, info);
}
