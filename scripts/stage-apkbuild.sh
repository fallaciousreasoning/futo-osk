#!/bin/sh
# Writes the APKBUILD and a tarball of this working tree into DIR, ready
# for abuild (or pmbootstrap). The APKBUILD is pointed at the tarball
# instead of the release on GitHub, so unreleased changes build too; bump
# pkgrel when they should replace an installed build.
#   scripts/stage-apkbuild.sh DIR
set -eu
src=$(cd "$(dirname "$0")/.." && pwd)
dest=$1
version=$(sed -n "s/^pkgver=//p" "$src/packaging/alpine/APKBUILD")

mkdir -p "$dest"
cp "$src/packaging/alpine/APKBUILD" "$dest/"
cd "$src"
git ls-files -co --exclude-standard |
    tar -czf "$dest/futo-osk-$version.tar.gz" --transform "s,^,futo-osk-$version/," -T -
sum=$(sha512sum "$dest/futo-osk-$version.tar.gz" | cut -d' ' -f1)
sed -i -e 's|^source="$pkgname-$pkgver.tar.gz::.*|source="$pkgname-$pkgver.tar.gz|' \
    -e "s/^[0-9a-fA-FSKIP]*  futo-osk-$version.tar.gz\$/$sum  futo-osk-$version.tar.gz/" \
    "$dest/APKBUILD"
echo "Staged $dest"
