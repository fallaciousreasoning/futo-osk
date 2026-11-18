#include "org_futo_inputmethod_latin_DicTraverseSession.cpp"
#include "glue.h"

jlong futoglue::session_new(JNIEnv *env, jstring locale, jlong dict_size) {
    return latinime::latinime_setDicTraverseSession(env, nullptr, locale, dict_size);
}

void futoglue::session_init(JNIEnv *env, jlong session, jlong dict) {
    latinime::latinime_initDicTraverseSession(env, nullptr, session, dict, nullptr, 0);
}

void futoglue::session_free(JNIEnv *env, jlong session) {
    latinime::latinime_releaseDicTraverseSession(env, nullptr, session);
}
