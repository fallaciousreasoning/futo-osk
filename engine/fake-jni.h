/*
 * A tiny in-process stand-in for a Java VM, just big enough to drive
 * FUTO Keyboard's native JNI glue without Java.
 *
 * Every object handed to the glue is owned by the Env and lives until
 * Env::clear(). Calling a JNI function that isn't implemented aborts.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "jni.h"

namespace fakejni {

struct Obj {
    virtual ~Obj() = default;
};

struct Str : Obj {
    std::string s;
};

template <typename T> struct Arr : Obj {
    std::vector<T> v;
};

using IntArr = Arr<jint>;
using FloatArr = Arr<jfloat>;
using BoolArr = Arr<jboolean>;
using ObjArr = Arr<jobject>;

class Env {
public:
    Env();
    Env(const Env &) = delete;
    Env &operator=(const Env &) = delete;

    JNIEnv *jni() { return &env_; }

    jstring str(const std::string &s);
    jintArray ints(size_t n, const jint *init = nullptr);
    jfloatArray floats(size_t n, const jfloat *init = nullptr);
    jbooleanArray bools(size_t n);
    jobjectArray objs(size_t n);

    /* Frees every object created since the last clear(). */
    void clear() { owned_.clear(); }

    template <typename T> T *adopt(T *o) {
        owned_.emplace_back(o);
        return o;
    }

private:
    JNINativeInterface table_;
    JNIEnv env_;
    std::vector<std::unique_ptr<Obj>> owned_;
};

/* Typed views of handles. */
inline IntArr &ints(jobject o) { return *static_cast<IntArr *>(reinterpret_cast<Obj *>(o)); }
inline FloatArr &floats(jobject o) { return *static_cast<FloatArr *>(reinterpret_cast<Obj *>(o)); }
inline ObjArr &objs(jobject o) { return *static_cast<ObjArr *>(reinterpret_cast<Obj *>(o)); }
inline const std::string *str(jobject o) {
    return o ? &static_cast<Str *>(reinterpret_cast<Obj *>(o))->s : nullptr;
}

} // namespace fakejni
