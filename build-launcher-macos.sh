#!/bin/sh
# Rebuild only the Windows launcher on macOS with an installed/extracted Zig 0.15.2.
set -eu
if [ "$#" -ne 1 ]; then
    echo 'Usage: sh build-macos.sh /absolute/path/to/zig' >&2
    exit 2
fi
PDFROTATE_ZIG="$1"
PDFROTATE_SOURCE_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
PDFROTATE_PACKAGE_DIR="$(dirname -- "$PDFROTATE_SOURCE_DIR")"
PDFROTATE_BUILD_DIR="$PDFROTATE_PACKAGE_DIR/.build"
mkdir -p "$PDFROTATE_BUILD_DIR"
export ZIG_GLOBAL_CACHE_DIR="$PDFROTATE_BUILD_DIR/zig-cache"
export ZIG_LOCAL_CACHE_DIR="$PDFROTATE_BUILD_DIR/zig-local-cache"
cd "$PDFROTATE_SOURCE_DIR"
"$PDFROTATE_ZIG" rc /fo "$PDFROTATE_BUILD_DIR/app.res" app.rc
"$PDFROTATE_ZIG" cc -target x86_64-windows-gnu -municode -Wl,--subsystem,windows -std=c11 \
    -O2 -Wall -Wextra -s PDFRotate.c "$PDFROTATE_BUILD_DIR/app.res" \
    -lcomdlg32 -lcomctl32 -lshell32 -luser32 -lgdi32 \
    -o "$PDFROTATE_PACKAGE_DIR/PDFRotate.exe"
