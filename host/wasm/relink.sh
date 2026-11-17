#!/usr/bin/env bash
#
# Rebuild hostfn.wasm from the Wine-derived sources beside this script and the objects in obj/.
#
# This is the LGPL-2.1 section 6a relinking step for the Windows plug-in host's function module.
# monodither.c, drawedge.c, cxxeh.c and codepage20127.c are derived from Wine (LGPL-2.1+) and are statically linked
# into hostfn.wasm with the host's own code, which is supplied here only as object files (obj/*.o).
# Edit the derived sources as you like, run this, and the result is a hostfn.wasm with your version
# of them linked in.
#
#   obj/compile.args   the flags each source is compiled with
#   obj/link.args      the link flags and EVERY input, in link order -- the derived files' objects
#                      among them, which this script writes to obj/ first
#
# Both files are written by the build that produced the shipped module, and that build links exactly
# as this does, so unmodified sources give the shipped bytes (obj/BUILT_FROM holds their SHA-256).
#
# Needs clang with the wasm32 target and wasm-ld: emsdk's `upstream/bin` has both (set EMSDK_DIR),
# or set CLANG to any clang that has them.
#
# Usage:  ./relink.sh            -> hostfn.wasm beside this script
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -z "${CLANG:-}" ]; then
	if [ -n "${EMSDK_DIR:-}" ]; then
		CLANG="$EMSDK_DIR/upstream/bin/clang"
		[ -x "$CLANG" ] || CLANG="$CLANG.exe"
	else
		CLANG=clang
	fi
fi

# A source beside this script replaces the object of the same name; link.args names every input.
for src in "$HERE"/*.c; do
	"$CLANG" @"$HERE/obj/compile.args" -c "$src" -o "$HERE/obj/$(basename "$src" .c).o"
done
(cd "$HERE" && "$CLANG" @obj/link.args -o hostfn.wasm)
echo "hostfn.wasm $(wc -c < "$HERE/hostfn.wasm") bytes"
