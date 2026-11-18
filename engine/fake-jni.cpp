#include "fake-jni.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fakejni {

namespace {

Env *self(JNIEnv *env) {
    return static_cast<Env *>(const_cast<void *>(env->functions->reserved0));
}

Obj *obj(jobject o) { return reinterpret_cast<Obj *>(o); }

[[noreturn]] void unimplemented() {
    fprintf(stderr, "fake-jni: called an unimplemented JNI function\n");
    abort();
}

/* Number of UTF-8 bytes in a sequence starting with this byte. */
size_t utf8_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xe) return 3;
    return 4;
}

/* Java strings are measured in UTF-16 units. */
jsize utf16_units(const std::string &s) {
    jsize n = 0;
    for (size_t i = 0; i < s.size();) {
        size_t l = utf8_len(s[i]);
        n += l == 4 ? 2 : 1;
        i += l;
    }
    return n;
}

/* Byte offset of the UTF-16 unit index `units` in s. */
size_t utf8_offset(const std::string &s, jsize units) {
    size_t i = 0;
    while (i < s.size() && units > 0) {
        size_t l = utf8_len(s[i]);
        units -= l == 4 ? 2 : 1;
        i += l;
    }
    return std::min(i, s.size());
}

template <typename T>
void get_region(jobject a, jsize start, jsize len, T *buf) {
    auto &v = static_cast<Arr<T> *>(obj(a))->v;
    memcpy(buf, v.data() + start, sizeof(T) * len);
}

template <typename T>
void set_region(jobject a, jsize start, jsize len, const T *buf) {
    auto &v = static_cast<Arr<T> *>(obj(a))->v;
    memcpy(v.data() + start, buf, sizeof(T) * len);
}

template <typename T> T *elements(jobject a, jboolean *is_copy) {
    if (is_copy) *is_copy = JNI_FALSE;
    return static_cast<Arr<T> *>(obj(a))->v.data();
}

} // namespace

Env::Env() : table_(), env_(), owned_() {
    void **slots = reinterpret_cast<void **>(&table_);
    for (size_t i = 0; i < sizeof(table_) / sizeof(void *); i++)
        slots[i] = reinterpret_cast<void *>(&unimplemented);
    table_.reserved0 = this;

    table_.GetStringLength = [](JNIEnv *, jstring s) -> jsize { return utf16_units(*fakejni::str(s)); };
    table_.GetStringUTFLength = [](JNIEnv *, jstring s) -> jsize { return (jsize)fakejni::str(s)->size(); };
    table_.GetStringUTFRegion = [](JNIEnv *, jstring s, jsize start, jsize len, char *buf) {
        const std::string &v = *fakejni::str(s);
        size_t from = utf8_offset(v, start);
        size_t to = from + utf8_offset(v.substr(from), len);
        memcpy(buf, v.data() + from, to - from);
        buf[to - from] = '\0';
    };
    table_.GetStringUTFChars = [](JNIEnv *, jstring s, jboolean *is_copy) -> const char * {
        if (is_copy) *is_copy = JNI_FALSE;
        return fakejni::str(s)->c_str();
    };
    table_.ReleaseStringUTFChars = [](JNIEnv *, jstring, const char *) {};
    table_.NewStringUTF = [](JNIEnv *env, const char *s) -> jstring { return self(env)->str(s); };

    table_.GetArrayLength = [](JNIEnv *, jarray a) -> jsize {
        /* Every array type shares the vector layout of Arr<T>; size() needs the element size. */
        Obj *o = obj(a);
        if (auto *i = dynamic_cast<IntArr *>(o)) return (jsize)i->v.size();
        if (auto *f = dynamic_cast<FloatArr *>(o)) return (jsize)f->v.size();
        if (auto *b = dynamic_cast<BoolArr *>(o)) return (jsize)b->v.size();
        if (auto *x = dynamic_cast<ObjArr *>(o)) return (jsize)x->v.size();
        unimplemented();
    };
    table_.GetObjectArrayElement = [](JNIEnv *, jobjectArray a, jsize i) -> jobject {
        return fakejni::objs(a).v.at(i);
    };
    table_.SetObjectArrayElement = [](JNIEnv *, jobjectArray a, jsize i, jobject v) {
        fakejni::objs(a).v.at(i) = v;
    };
    table_.DeleteLocalRef = [](JNIEnv *, jobject) {};
    table_.ExceptionCheck = [](JNIEnv *) -> jboolean { return JNI_FALSE; };
    table_.ExceptionClear = [](JNIEnv *) {};

    table_.NewIntArray = [](JNIEnv *env, jsize n) -> jintArray { return self(env)->ints(n); };
    table_.NewFloatArray = [](JNIEnv *env, jsize n) -> jfloatArray { return self(env)->floats(n); };
    table_.NewBooleanArray = [](JNIEnv *env, jsize n) -> jbooleanArray { return self(env)->bools(n); };

    table_.GetIntArrayRegion = [](JNIEnv *, jintArray a, jsize s, jsize l, jint *b) { get_region(a, s, l, b); };
    table_.SetIntArrayRegion = [](JNIEnv *, jintArray a, jsize s, jsize l, const jint *b) { set_region(a, s, l, b); };
    table_.GetFloatArrayRegion = [](JNIEnv *, jfloatArray a, jsize s, jsize l, jfloat *b) { get_region(a, s, l, b); };
    table_.SetFloatArrayRegion = [](JNIEnv *, jfloatArray a, jsize s, jsize l, const jfloat *b) { set_region(a, s, l, b); };
    table_.GetBooleanArrayRegion = [](JNIEnv *, jbooleanArray a, jsize s, jsize l, jboolean *b) { get_region(a, s, l, b); };
    table_.SetBooleanArrayRegion = [](JNIEnv *, jbooleanArray a, jsize s, jsize l, const jboolean *b) { set_region(a, s, l, b); };

    table_.GetIntArrayElements = [](JNIEnv *, jintArray a, jboolean *c) { return elements<jint>(a, c); };
    table_.ReleaseIntArrayElements = [](JNIEnv *, jintArray, jint *, jint) {};
    table_.GetFloatArrayElements = [](JNIEnv *, jfloatArray a, jboolean *c) { return elements<jfloat>(a, c); };
    table_.ReleaseFloatArrayElements = [](JNIEnv *, jfloatArray, jfloat *, jint) {};

    env_.functions = &table_;
}

jstring Env::str(const std::string &s) {
    auto *o = adopt(new Str());
    o->s = s;
    return reinterpret_cast<jstring>(static_cast<Obj *>(o));
}

template <typename T, typename H> static H make_array(Env *env, size_t n, const T *init) {
    auto *o = env->adopt(new Arr<T>());
    o->v.assign(n, T());
    if (init) memcpy(o->v.data(), init, sizeof(T) * n);
    return reinterpret_cast<H>(static_cast<Obj *>(o));
}

jintArray Env::ints(size_t n, const jint *init) { return make_array<jint, jintArray>(this, n, init); }
jfloatArray Env::floats(size_t n, const jfloat *init) { return make_array<jfloat, jfloatArray>(this, n, init); }
jbooleanArray Env::bools(size_t n) { return make_array<jboolean, jbooleanArray>(this, n, nullptr); }
jobjectArray Env::objs(size_t n) {
    auto *o = adopt(new ObjArr());
    o->v.assign(n, nullptr);
    return reinterpret_cast<jobjectArray>(static_cast<Obj *>(o));
}

} // namespace fakejni

/* FUTO's jni_utils.cpp goes through java.nio.Charset; plain UTF-8 is enough here. */
std::string jstring2string(JNIEnv *, jstring s) {
    const std::string *v = fakejni::str(s);
    return v ? *v : std::string();
}

jstring string2jstring(JNIEnv *env, const char *s) {
    return env->NewStringUTF(s);
}

namespace latinime {
int registerNativeMethods(JNIEnv *, const char *, const JNINativeMethod *, const int) { return 0; }
} // namespace latinime
