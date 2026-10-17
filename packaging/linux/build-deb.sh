#!/usr/bin/env bash
# Build a .deb package for Debian/Ubuntu: ./packaging/linux/build-deb.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
VERSION=$(grep -oP 'MIRC_VERSION "\K[^"]+' src/mirc.h)
ARCH=$(dpkg --print-architecture)
ROOT=build/deb/mirc-client_${VERSION}_${ARCH}
rm -rf "$ROOT"
make
make install DESTDIR="$ROOT" PREFIX=/usr
strip "$ROOT/usr/bin/mirc"
mkdir -p "$ROOT/DEBIAN"
cat > "$ROOT/DEBIAN/control" <<CTRL
Package: mirc-client
Version: ${VERSION}
Section: net
Priority: optional
Architecture: ${ARCH}
Depends: libgtk-3-0 (>= 3.22), libsoup-3.0-0, libjson-glib-1.0-0, glib-networking
Maintainer: MIRC <noreply@localhost>
Description: MIRC – Modern Internet Relay Chat (Client)
 Desktop client for MIRC chat servers: channels, direct messages,
 name colors, avatars, and themes. Sign in through a MIRC session server.
CTRL
dpkg-deb --root-owner-group --build "$ROOT" >/dev/null
echo "Paket: $ROOT.deb"
