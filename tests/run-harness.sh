#!/bin/sh
# Runs the typing harness headlessly on GTK's Broadway backend.
#   tests/run-harness.sh BUILD_DIR DATA_DIR [SCREENSHOT_PREFIX]
set -e
build=$1; shift
display=:${HARNESS_DISPLAY:-17}
gtk4-broadwayd "$display" >/dev/null 2>&1 &
daemon=$!
trap 'kill $daemon' EXIT
sleep 0.5
GDK_BACKEND=broadway BROADWAY_DISPLAY=$display GSETTINGS_BACKEND=memory \
    "$build/tests/harness" "$@"
