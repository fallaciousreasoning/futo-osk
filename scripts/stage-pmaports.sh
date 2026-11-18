#!/bin/sh
# Copies the APKBUILD and a tarball of this working tree into a pmaports
# checkout, ready for: pmbootstrap build futo-osk
#   scripts/stage-pmaports.sh ~/dev/pmaports [temp]
set -eu
src=$(cd "$(dirname "$0")/.." && pwd)
dest=$1/${2:-temp}/futo-osk
version=$(sed -n "s/^pkgver=//p" "$src/packaging/alpine/APKBUILD")

mkdir -p "$dest"
cp "$src/packaging/alpine/APKBUILD" "$dest/"
cd "$src"
git ls-files -co --exclude-standard |
    tar -czf "$dest/futo-osk-$version.tar.gz" --transform "s,^,futo-osk-$version/," -T -
sum=$(sha512sum "$dest/futo-osk-$version.tar.gz" | cut -d' ' -f1)
sed -i "s/^SKIP  futo-osk-/$sum  futo-osk-/" "$dest/APKBUILD"
echo "Staged $dest"
