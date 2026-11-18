#!/bin/sh
# Stages both packages into a pmaports checkout, ready for:
#   pmbootstrap build --arch aarch64 futo-osk futo-swipe-decoder
#   scripts/stage-pmaports.sh ~/dev/pmaports [temp]
set -eu
for pkg in futo-osk futo-swipe-decoder; do
    "$(dirname "$0")/stage-apkbuild.sh" "$1/${2:-temp}/$pkg" "$pkg"
done
