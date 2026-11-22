#!/bin/sh
# Builds a Debian package of M.I.K.E:  sh linux/build-deb.sh   ->  mike_1.0_<arch>.deb
# Needs: sudo apt install build-essential libsdl2-dev libgl-dev dpkg-dev
# Install it with:  sudo apt install ./mike_1.0_<arch>.deb   (pulls in libsdl2 by itself)
set -e
cd "$(dirname "$0")/.."
VER=1.0
ARCH=$(dpkg --print-architecture)
PKG=build/deb/mike_${VER}_${ARCH}
rm -rf "$PKG"
make clean
make PREFIX=/usr
make PREFIX=/usr DESTDIR="$PKG" install
# Debian puts games in /usr/games (it's on everyone's PATH)
SIZE=$(du -sk "$PKG/usr" | cut -f1)
mkdir -p "$PKG/DEBIAN"
cat > "$PKG/DEBIAN/control" <<CTL
Package: mike
Version: $VER
Section: games
Priority: optional
Architecture: $ARCH
Depends: libsdl2-2.0-0, libgl1, libc6
Installed-Size: $SIZE
Maintainer: Prosystems Corp <clipxp.com>
Homepage: https://clipxp.com/mike/
Description: M.I.K.E - Multimedia Interactive Kinetic Element
 A 3D obby platformer starring M.I.K.E, the TV, and A.L.E.X.
 Jump across 150 courses over the lava, race, play endless mode,
 build your own levels and look after your VMU pet.
CTL
fakeroot dpkg-deb --build "$PKG" . 2>/dev/null || dpkg-deb --root-owner-group --build "$PKG" .
make clean
ls -l mike_${VER}_${ARCH}.deb
