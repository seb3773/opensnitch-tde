#!/usr/bin/env bash
set -euo pipefail

PKG_NAME="OpenSnitch-tde"
PKG_VERSION="1.5.8"
PKG_MAINTAINER="seb3773"
PKG_SECTION="admin"
PKG_PRIORITY="optional"

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
ARCH="$(dpkg --print-architecture)"
BUILD_DIR="$SRC_ROOT/build"
PKGROOT="$BUILD_DIR/pkgroot"
PKGTMP="$BUILD_DIR/pkgtmp"

need_cmd() {
	command -v "$1" >/dev/null 2>&1 || {
		echo "error: missing required command: $1" >&2
		exit 1
	}
}

need_cmd cmake
need_cmd pkg-config
need_cmd protoc
need_cmd grpc_cpp_plugin
need_cmd dpkg-deb
need_cmd strip
need_cmd sed
need_cmd awk
need_cmd du
need_cmd ln
need_cmd install

mkdir -p -- "$BUILD_DIR"
rm -rf -- "$PKGTMP"
mkdir -p -- "$PKGTMP"

# Build (out-of-tree).
"$SRC_ROOT/build.sh"

BIN_PATH="$BUILD_DIR/opensnitch-tde"
if test ! -x "$BIN_PATH"; then
	echo "error: missing built binary: $BIN_PATH" >&2
	exit 1
fi

# Debian requires lowercase package name.
PKG_DEB_NAME="$(printf '%s' "$PKG_NAME" | tr '[:upper:]' '[:lower:]')"

# Stage filesystem layout.
rm -rf -- "$PKGROOT"
mkdir -p -- \
	"$PKGROOT/DEBIAN" \
	"$PKGROOT/usr/bin" \
	"$PKGROOT/usr/share/applications" \
	"$PKGROOT/usr/share/icons/hicolor"

# Install binary.
install -m 0755 "$BIN_PATH" "$PKGROOT/usr/bin/opensnitch-tde"

# Install desktop entry.
DESKTOP_SRC="$SRC_ROOT/opensnitch_tde.desktop"
if test -f "$DESKTOP_SRC"; then
	install -m 0644 "$DESKTOP_SRC" "$PKGROOT/usr/share/applications/opensnitch_tde.desktop"
else
	echo "warning: missing $DESKTOP_SRC (desktop entry will not be installed)" >&2
fi

# Install application icon tree.
# The .desktop file uses Icon=opensnitchtde.png
ICON_SRC="$SRC_ROOT/icons/opensnitch_icon.png"
if test -f "$ICON_SRC"; then
	real_sz="64x64"
	real_dir="$PKGROOT/usr/share/icons/hicolor/$real_sz/apps"
	mkdir -p -- "$real_dir"
	install -m 0644 "$ICON_SRC" "$real_dir/opensnitchtde.png"
	for sz in 16x16 22x22 24x24 32x32 48x48; do
		dstdir="$PKGROOT/usr/share/icons/hicolor/$sz/apps"
		mkdir -p -- "$dstdir"
		ln -sf "../../$real_sz/apps/opensnitchtde.png" "$dstdir/opensnitchtde.png"
	done
else
	echo "warning: missing $ICON_SRC (application icon will not be installed)" >&2
fi

# Specify explicit optimal runtime dependencies
DEPENDS="tdelibs14-trinity (>= 4:14.1.1), opensnitch"

# Strip the staged binary
STAGED_BIN="$PKGROOT/usr/bin/opensnitch-tde"
if command -v sstrip >/dev/null 2>&1; then
	echo "info: stripping with sstrip"
	sstrip "$STAGED_BIN" >/dev/null 2>&1 || true
else
	echo "info: sstrip not found, using strip --strip-all"
	strip --strip-all "$STAGED_BIN" >/dev/null 2>&1 || true
fi

# Debian control file.
INSTALLED_SIZE_KB="$(du -sk "$PKGROOT/usr" | awk '{print $1}')"
cat > "$PKGROOT/DEBIAN/control" <<EOF
Package: $PKG_DEB_NAME
Version: $PKG_VERSION
Section: $PKG_SECTION
Priority: $PKG_PRIORITY
Architecture: $ARCH
Maintainer: $PKG_MAINTAINER
Installed-Size: $INSTALLED_SIZE_KB
Depends: $DEPENDS
Description: OpenSnitch UI (TQt3/TDE) native port
 Native TQt3 / Trinity Desktop UI port for OpenSnitch.
EOF

# postinst: configure APT repository and refresh icon cache if available.
cat > "$PKGROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

# Configuration automatique du dépôt APT pour les futures mises à jour
if [ -d /etc/apt/sources.list.d ]; then
	cat << 'REPEOF' > /etc/apt/sources.list.d/opensnitch-tde.list
# OpenSnitch-TDE APT Repository
deb [trusted=yes] https://seb3773.github.io/opensnitch-tde/ stable main
REPEOF
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
	gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$PKGROOT/DEBIAN/postinst"

# prerm: refresh icon cache on removal (best effort).
cat > "$PKGROOT/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
	gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$PKGROOT/DEBIAN/prerm"

# postrm: remove APT repository configuration and refresh caches.
cat > "$PKGROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "purge" ] || [ "$1" = "remove" ]; then
	rm -f /etc/apt/sources.list.d/opensnitch-tde.list
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
	gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$PKGROOT/DEBIAN/postrm"

OUT_DEB="$SRC_ROOT/${PKG_DEB_NAME}_${PKG_VERSION}_${ARCH}.deb"
rm -f -- "$OUT_DEB"

dpkg-deb --build "$PKGROOT" "$OUT_DEB" >/dev/null

echo "$OUT_DEB"
