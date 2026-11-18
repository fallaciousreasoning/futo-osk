#!/bin/sh
# Builds the Alpine packages in an Alpine edge container, with podman or
# docker, on any Linux machine.
#   scripts/build-alpine.sh [-a ARCH] [OUTDIR]
#
# ARCH defaults to the host's. Building for another one (e.g. -a aarch64
# on a PC) needs qemu-user binfmt support, and is slow.
# The APKs land in OUTDIR/ARCH (default: ./packages), signed with a
# throwaway key saved next to them.
set -eu

arch=
while getopts a: opt; do
    case $opt in
    a) arch=$OPTARG ;;
    *) echo "usage: $0 [-a ARCH] [OUTDIR]" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
out=$(realpath -m "${1:-packages}")

engine=$(command -v podman || command -v docker) || {
    echo "Needs podman or docker" >&2
    exit 1
}
platform=
case $arch in
'') ;;
aarch64) platform="--platform linux/arm64" ;;
x86_64) platform="--platform linux/amd64" ;;
*) platform="--platform linux/$arch" ;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$(dirname "$0")/stage-apkbuild.sh" "$work/futo-osk"
mkdir -p "$out"

# shellcheck disable=SC2086
"$engine" run --rm $platform \
    -v "$work:/work" -v "$out:/out" \
    docker.io/library/alpine:edge sh -euc '
        apk add --no-progress -q alpine-sdk sudo
        adduser -D builder
        addgroup builder abuild
        echo "builder ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/builder
        cp -r /work/futo-osk /home/builder/
        chown -R builder:builder /home/builder/futo-osk
        su builder -c "
            set -e
            abuild-keygen -anq
            cd ~/futo-osk
            REPODEST=/tmp/repo abuild -r
            cp -r /tmp/repo/futo-osk/* /out/
            cp ~/.abuild/*.rsa.pub /out/
        "
        chown -R '"$(id -u):$(id -g)"' /out
    '
echo "Packages in $out"
ls -R "$out"
