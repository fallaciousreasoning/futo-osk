#!/bin/sh
# Fetches the pinned FUTO Keyboard source and its English dictionary and
# model into futo/. Both are under the FUTO Source First License:
# non-commercial use only; see futo/android-keyboard/LICENSE.md.
set -eu

FUTO_COMMIT=7d61ab4132cfcbb7aa713190d934f0125dc38b03
RESOURCES_COMMIT=d87d9dbdf3966bbe18413be375dab2f6c7bbdfdd
DICT_SHA256=c3e21185c056e82769a2bb1381ae6b0e799437bdab101379715c6baa0a3544d1
MODEL_SHA256=6545c1c9ef2d76e9bfb87ad4fcf2061889513af84fcf30d907412be7fcdedb7b

mkdir -p "$(dirname "$0")/../futo"
cd "$(dirname "$0")/../futo"

fetch() { # url dir
    [ -d "$2" ] && return
    echo "Fetching $1"
    mkdir "$2.tmp"
    curl -fL "$1" | tar -xz -C "$2.tmp" --strip-components=1
    mv "$2.tmp" "$2"
}

fetch "https://github.com/futo-org/android-keyboard/archive/$FUTO_COMMIT.tar.gz" android-keyboard
fetch "https://gitlab.futo.org/keyboard/keyboard-large-resources/-/archive/$RESOURCES_COMMIT/keyboard-large-resources-$RESOURCES_COMMIT.tar.gz" resources

echo "$DICT_SHA256  resources/raw/main_en.dict" | sha256sum -c -
echo "$MODEL_SHA256  resources/raw/ml4_q6_k.gguf" | sha256sum -c -
