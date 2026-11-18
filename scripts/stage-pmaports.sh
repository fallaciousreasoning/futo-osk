#!/bin/sh
# Stages the package into a pmaports checkout, ready for:
#   pmbootstrap build --arch aarch64 futo-osk
#   scripts/stage-pmaports.sh ~/dev/pmaports [temp]
set -eu
exec "$(dirname "$0")/stage-apkbuild.sh" "$1/${2:-temp}/futo-osk"
