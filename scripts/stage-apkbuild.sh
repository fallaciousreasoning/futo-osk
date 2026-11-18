#!/bin/sh
# Writes an APKBUILD from packaging/alpine/ and a tarball of this working
# tree into DIR, ready for abuild (or pmbootstrap). The APKBUILD is pointed
# at the tarball instead of the release on GitHub, so unreleased changes
# build too; bump pkgrel when they should replace an installed build.
#   scripts/stage-apkbuild.sh DIR [PACKAGE]    (default: futo-osk)
set -eu
src=$(cd "$(dirname "$0")/.." && pwd)
dest=$1
pkg=${2:-futo-osk}
apkbuild=$src/packaging/alpine/$pkg/APKBUILD
version=$(sed -n "s/^pkgver=//p" "$apkbuild")
tarball=futo-osk-$version.tar.gz

mkdir -p "$dest"
cp "$apkbuild" "$dest/"
cd "$src"
git ls-files -co --exclude-standard |
    tar -czf "$dest/$tarball" --transform "s,^,futo-osk-$version/," -T -
sum=$(sha512sum "$dest/$tarball" | cut -d' ' -f1)
sed -i -e 's|::https://github.com/fallaciousreasoning/futo-osk/archive/refs/tags/v$pkgver.tar.gz||' \
    -e "s/^[0-9a-fA-FSKIP]*  $tarball\$/$sum  $tarball/" \
    "$dest/APKBUILD"
echo "Staged $dest"
