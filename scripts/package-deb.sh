#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-}"
PROJECT_VERSION="$(sed -n 's/^project(simplescreenrecorder VERSION \([^)]*\)).*/\1/p' "$ROOT_DIR/CMakeLists.txt")"

if [[ -z "$VERSION" || "$VERSION" != "$PROJECT_VERSION" ]]; then
	echo "Usage: $0 $PROJECT_VERSION" >&2
	exit 2
fi

BUILD_DIR="${SSR_BUILD_DIR:-$ROOT_DIR/build-release}"
STAGING_DIR="$BUILD_DIR/packages/staging"
PACKAGE_ROOT="$BUILD_DIR/packages/root"
ARCH="$(dpkg --print-architecture)"

rm -rf "$STAGING_DIR" "$PACKAGE_ROOT"
mkdir -p "$STAGING_DIR" "$PACKAGE_ROOT/DEBIAN"

DESTDIR="$STAGING_DIR" cmake --install "$BUILD_DIR" --prefix /usr
cp -a "$STAGING_DIR/usr/." "$PACKAGE_ROOT/"

BIN="$PACKAGE_ROOT/usr/bin/simplescreenrecorder"
if [[ ! -x "$BIN" ]]; then
	BIN="$PACKAGE_ROOT/bin/simplescreenrecorder"
	if [[ ! -x "$BIN" ]]; then
		echo "Expected executable missing from staged install" >&2
		exit 1
	fi
	LIB_ROOT="$PACKAGE_ROOT/lib"
else
	LIB_ROOT="$PACKAGE_ROOT/usr/lib"
fi
GLINJECT="$(find "$LIB_ROOT" -name libssr-glinject.so -type f -print -quit)"
if [[ -z "$GLINJECT" ]]; then
	echo "Expected executable or GLInject library missing from staged install" >&2
	exit 1
fi

mkdir -p "$BUILD_DIR/packages/debian"
cat > "$BUILD_DIR/packages/debian/control" <<EOF
Source: simplescreenrecorder
Section: video
Priority: optional
Maintainer: SimpleScreenRecorder contributors
Build-Depends: debhelper-compat (= 13)
Standards-Version: 4.6.2

Package: simplescreenrecorder
Architecture: any
Description: Screen recorder for Linux
 SimpleScreenRecorder is a screen recorder for Linux systems using X11.
EOF

SHLIBS="$(cd "$BUILD_DIR/packages" && dpkg-shlibdeps -O --ignore-missing-info -e"$BIN" -e"$GLINJECT")"
DEPENDS="${SHLIBS#*=}"
if [[ "$DEPENDS" == "$SHLIBS" || -z "$DEPENDS" ]]; then
	echo "Could not determine runtime package dependencies" >&2
	exit 1
fi

cat > "$PACKAGE_ROOT/DEBIAN/control" <<EOF
Package: simplescreenrecorder
Version: $VERSION
Section: video
Priority: optional
Architecture: $ARCH
Depends: $DEPENDS
Maintainer: SimpleScreenRecorder contributors
Description: Screen recorder for Linux
 SimpleScreenRecorder is a screen recorder for Linux systems using X11.
EOF

dpkg-deb --root-owner-group --build "$PACKAGE_ROOT" \
	"$BUILD_DIR/packages/simplescreenrecorder_${VERSION}_${ARCH}.deb"