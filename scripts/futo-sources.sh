#!/bin/sh
# Prints the core engine sources listed in FUTO's NativeFileList.cmake,
# relative to native/jni, one per line.
set -e
awk '/^set\(LATIN_IME_CORE_SRC_FILES/ { on = 1; next }
     on { last = /\)/; sub(/.*\$\{CMAKE_CURRENT_SOURCE_DIR\}\//, ""); sub(/\)?[ \t]*$/, "");
          if ($0 != "") print; if (last) on = 0 }' \
    "$1/native/jni/NativeFileList.cmake"
