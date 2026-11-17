# wine-plugin-host

The parts of a Windows plug-in host (an emulator that runs 32-bit Windows image plug-ins in a
browser, as WebAssembly and JavaScript) that are ported or derived from **Wine**. Because they are
derived from Wine source, they are licensed under the **GNU LGPL-2.1-or-later** (see
[`LICENSE`](./LICENSE) and [`NOTICE`](./NOTICE)).

This repository is the "corresponding source" required by LGPL-2.1 section 6 for those parts,
which ship inside a closed-source application. The application itself is a separate work that
uses them and is not covered by this license.

## What's here

| Path | What | Wine source |
|------|------|-------------|
| `host/wasm/cxxeh.c` | msvcrt's `__CxxFrameHandler` (C++ exceptions, 32-bit x86) | `dlls/msvcrt/except_i386.c`, `except.c`, `cppexcept.h`, `cxx.h` |
| `host/wasm/codepage20127.c` | code page 20127's tables, the "C" locale's code page in Wine's msvcrt | `nls/c_20127.nls` |
| `host/wasm/formatmessage.c` | `FormatMessageA`/`FormatMessageW` | `dlls/kernelbase/locale.c`, `dlls/ntdll/rtlstr.c` |
| `host/wasm/winerror.c`, `tools/build-winerror.mjs` | the English system message table, and its generator | `dlls/kernelbase/winerror.mc` |
| `host/wasm/drawedge.c` | DrawEdge's rectangle edges | `dlls/user32/uitools.c` |
| `host/wasm/monodither.c` | a solid brush on a 1-bpp surface: the 8x8 ordered dither | `dlls/win32u/dibdrv/primitives.c`, `objects.c` |
| `win32/seh4.js` | msvcrt's `_except_handler4_common` (SEH4 frames) | `dlls/msvcrt/except_i386.c` |
| `win32/cxxeh.js` | the emulated CPU's side of `cxxeh.c` | `dlls/msvcrt/except_i386.c` |
| `win32/dialogManager.js` | `IsDialogMessage`, the tab and group walks, `WM_NEXTDLGCTL` | `dlls/user32/dialog.c`, `defdlg.c` |
| `win32/editText.js` | the EDIT control's typing, keys, undo buffer, clipboard and mouse | `dlls/user32/edit.c` |
| `win32/wine-cursors/` | the system cursors (`ocr_*.cur`, unchanged) and `build-wine-cursors.mjs`, which makes the shipped `wineCursors.js` from them | `dlls/user32/resources` |
| `host/wasm/relink.sh`, `host/wasm/obj/` | everything needed to relink `hostfn.wasm` (below) | -- |

Each derived file's header names the Wine files and functions it came from and says where and why
it differs from Wine (mostly: it runs as WebAssembly against an emulated CPU, and Windows itself,
not Wine, is the reference for behavior).

## The JavaScript files

`win32/seh4.js` and `win32/cxxeh.js` are loaded by the application as separate ES modules, so
they are replaced as they are: change the source and use it in place of the shipped module.
The same holds for the cursors: `node win32/wine-cursors/build-wine-cursors.mjs` regenerates
`wineCursors.js` (the shipped module, the `.cur` files as base64) from the `.cur` files beside it.

## The C files: rebuilding and relinking `hostfn.wasm` (LGPL-2.1 section 6a)

The three C files are **statically linked** into one WebAssembly module, `hostfn.wasm`, together
with the host's own C code. So that you can change the Wine-derived parts and still produce a
working module, this repository carries, in `host/wasm/obj/`:

- the **object files** (`*.o`, wasm32) of every other input of `hostfn.wasm` -- the host's own
  code, which is not published as source;
- `compile.args`, the flags every source is compiled with;
- `link.args`, the link flags and every input in link order;
- `BUILT_FROM`, the SHA-256 of the shipped `hostfn.wasm` these objects were taken from.

plus the three headers `cxxeh.c` includes (`hostimports.h`, `guest.h`, `layout.h`).

To rebuild, you need `clang` with the `wasm32` target and `wasm-ld` -- the ones in an
[emsdk](https://emscripten.org/) install (`upstream/bin`) are what the shipped module was built
with:

```sh
EMSDK_DIR=/path/to/emsdk ./host/wasm/relink.sh     # or CLANG=/path/to/clang
# -> host/wasm/hostfn.wasm
```

`relink.sh` compiles each `.c` beside it into `obj/` and links `obj/` exactly as the shipped
build does. With the sources unmodified the output is byte-identical to the shipped module
(compare its SHA-256 with `obj/BUILT_FROM`); with your changes it is the same module with your
version of the Wine-derived code. The module exports and imports are declared in the sources
(`EXPORT(...)` and `hostimports.h`); keep those signatures for the host to load your build.

The application loads `hostfn.wasm` from a JavaScript module that holds its bytes as base64
(`hostfn.wasm.js`, `export const HOSTFN_WASM_BASE64 = "..."`); replace that string with your
module's bytes in base64 to run your build.

## Provenance

Wine (c) the Wine project authors, https://www.winehq.org/. See `NOTICE` for each file's
source and copyright holders.
