/*
 * GUEST STRUCT LAYOUTS -- each struct the guest shares with us that holds a pointer, a handle or a
 * pointer-sized integer, DESCRIBED ONCE as a field list and expanded per ABI
 * (briefs/review_np_2026-11/x64_pass.md R3).
 *
 * A description is `GL_<NAME>(FIELD, FIELDS)`: `FIELD(name, kind)` is one field, `FIELDS(name,
 * kind, count)` an array of them. `GL_STRUCT(NAME, abi)` expands it for one ABI into
 * `struct GL_<NAME>_<abi>`, laid out by the compiler with the target's natural alignment, which is
 * MSVC's for both x86 and x64 (no struct here is packed). C reads an offset with `GL_OFF(NAME,
 * field)` and a size with `GL_BYTES(NAME)`, both for the ABI this build runs (x86).
 *
 * The kinds (R1): U8 U16 U32 I32 U64 V128 have one width everywhere; PTR (an address), WORD
 * (WPARAM, LPARAM, LONG_PTR ...), SIZE (SIZE_T) and HANDLE are 4 bytes on x86 and 8 on x64.
 *
 * THE JS CONSTANTS ARE GENERATED FROM THIS FILE: tools/gen-structlayout.mjs, which build.sh runs
 * first, reads these lists and writes host/structLayout.js, and writes guestlayout_check.h (guest.h
 * includes it), whose
 * `_Static_assert`s hold the compiler's offsets to the generator's -- so a list the two read
 * differently fails the C build. A struct is added here, never as `#define X_OFF 20`.
 * The generator reads only the description lines and `GL_STRUCT` lines: keep them to that form.
 */

#ifndef HOSTFN_GUESTLAYOUT_H
#define HOSTFN_GUESTLAYOUT_H

#include "structlayout.h"

/* MSG (winuser.h): 28 bytes on x86, 48 on x64. pt is a POINT, two LONGs. */
#define GL_MSG(FIELD, FIELDS) \
	FIELD(hwnd,    HANDLE) \
	FIELD(message, U32) \
	FIELD(wParam,  WORD) \
	FIELD(lParam,  WORD) \
	FIELD(time,    U32) \
	FIELD(pt_x,    I32) \
	FIELD(pt_y,    I32)
GL_STRUCT(MSG, x86)
GL_STRUCT(MSG, x64)

/*
 * CONTEXT (winnt.h, i386): 0x2cc bytes. x86 only -- the AMD64 CONTEXT is another struct, not this
 * list at another width, and gets its own description when an x64 engine needs it.
 */
#define GL_CONTEXT(FIELD, FIELDS) \
	FIELD(ContextFlags, U32) \
	FIELD(Dr0, U32) \
	FIELD(Dr1, U32) \
	FIELD(Dr2, U32) \
	FIELD(Dr3, U32) \
	FIELD(Dr6, U32) \
	FIELD(Dr7, U32) \
	FIELDS(FloatSave, U8, 112) \
	FIELD(SegGs, U32) \
	FIELD(SegFs, U32) \
	FIELD(SegEs, U32) \
	FIELD(SegDs, U32) \
	FIELD(Edi, U32) \
	FIELD(Esi, U32) \
	FIELD(Ebx, U32) \
	FIELD(Edx, U32) \
	FIELD(Ecx, U32) \
	FIELD(Eax, U32) \
	FIELD(Ebp, U32) \
	FIELD(Eip, U32) \
	FIELD(SegCs, U32) \
	FIELD(EFlags, U32) \
	FIELD(Esp, U32) \
	FIELD(SegSs, U32) \
	FIELDS(ExtendedRegisters, U8, 512)
GL_STRUCT(CONTEXT, x86)

/** The offset of `field` in the guest struct `S`, and its size, on the ABI this build runs. */
#define GL_OFF(S, field) ((u32)__builtin_offsetof(struct GL_##S##_x86, field))
#define GL_BYTES(S) ((u32)sizeof(struct GL_##S##_x86))

#endif
