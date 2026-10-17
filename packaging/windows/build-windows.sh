#!/usr/bin/env bash
# Build the MIRC client for Windows and create an installer (Setup.exe).
#
# Requirement: MSYS2 (https://www.msys2.org). Run this script in the
# "MSYS2 MINGW64" shell:
#     ./packaging/windows/build-windows.sh
set -euo pipefail
cd "$(dirname "$0")/../.."

pacman -S --needed --noconfirm make \
  mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf mingw-w64-x86_64-gtk3 \
  mingw-w64-x86_64-libsoup3 mingw-w64-x86_64-json-glib mingw-w64-x86_64-glib-networking \
  mingw-w64-x86_64-adwaita-icon-theme mingw-w64-x86_64-nsis

make clean
make
strip mirc.exe

DIST=build/windows/MIRC
rm -rf build/windows
mkdir -p "$DIST/bin" "$DIST/lib" "$DIST/share/glib-2.0/schemas" "$DIST/share/icons" "$DIST/etc/gtk-3.0"
cp mirc.exe "$DIST/bin/"

# Collect all required DLLs from MINGW64 (recursively via ldd).
copy_deps() {
  ldd "$1" | awk '{print $3}' | grep -i '^/mingw64/' | while read -r dll; do
    base=$(basename "$dll")
    [ -f "$DIST/bin/$base" ] || { cp "$dll" "$DIST/bin/"; copy_deps "$dll"; }
  done
}
copy_deps mirc.exe

# TLS for GIO (glib-networking) – without this module there are no TLS connections.
mkdir -p "$DIST/lib/gio/modules"
for m in /mingw64/lib/gio/modules/libgiognutls.dll /mingw64/lib/gio/modules/libgioopenssl.dll; do
  [ -f "$m" ] && cp "$m" "$DIST/lib/gio/modules/" && copy_deps "$m"
done

# Image formats (PNG, JPEG, GIF, WebP …)
cp -r /mingw64/lib/gdk-pixbuf-2.0 "$DIST/lib/"
for l in "$DIST"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do copy_deps "$l"; done

# Settings schemas, icons, default dark theme
cp /mingw64/share/glib-2.0/schemas/gschemas.compiled "$DIST/share/glib-2.0/schemas/"
cp -r /mingw64/share/icons/Adwaita /mingw64/share/icons/hicolor "$DIST/share/icons/"
mkdir -p "$DIST/share/icons/hicolor/scalable/apps"
cp packaging/mirc.svg "$DIST/share/icons/hicolor/scalable/apps/mirc.svg"
cat > "$DIST/etc/gtk-3.0/settings.ini" <<INI
[Settings]
gtk-application-prefer-dark-theme=true
gtk-font-name=Segoe UI 10
INI

cp packaging/windows/mirc.ico "$DIST/"
makensis -V2 packaging/windows/mirc.nsi
echo "Done: build/windows/MIRC-Setup-0.1.0.exe"
