/*
 * THE EXPANSION OF A FIELD-LIST DESCRIPTION (guestlayout.h, execblock.h) into a C struct per ABI.
 *
 * `GL_STRUCT(NAME, abi)` turns `GL_<NAME>(FIELD, FIELDS)` into `struct GL_<NAME>_<abi>`, each kind
 * at its width on that ABI. The struct is never laid over guest memory (x86 aligns nothing): it
 * exists so the compiler computes the offsets and the size, with MSVC's natural alignment.
 * tools/gen-structlayout.mjs applies the same widths for the JS constants.
 */

#ifndef HOSTFN_STRUCTLAYOUT_H
#define HOSTFN_STRUCTLAYOUT_H

#include "layout.h"

typedef struct { u64 lo, hi; } __attribute__((aligned(16))) gl_v128;

/* One width everywhere. */
#define GL_K_U8   unsigned char
#define GL_K_U16  unsigned short
#define GL_K_U32  u32
#define GL_K_I32  i32
#define GL_K_U64  u64
#define GL_K_V128 gl_v128

/* Pointer-sized: 4 bytes on x86, 8 on x64; host-side records hold them at 8 (R4). */
#define GL_K_x86_PTR    u32
#define GL_K_x86_WORD   u32
#define GL_K_x86_SIZE   u32
#define GL_K_x86_HANDLE u32
#define GL_K_x64_PTR    u64
#define GL_K_x64_WORD   u64
#define GL_K_x64_SIZE   u64
#define GL_K_x64_HANDLE u64
#define GL_K_host_PTR    u64
#define GL_K_host_WORD   u64
#define GL_K_host_SIZE   u64
#define GL_K_host_HANDLE u32
#define GL_K_x86_U8   GL_K_U8
#define GL_K_x86_U16  GL_K_U16
#define GL_K_x86_U32  GL_K_U32
#define GL_K_x86_I32  GL_K_I32
#define GL_K_x86_U64  GL_K_U64
#define GL_K_x86_V128 GL_K_V128
#define GL_K_x64_U8   GL_K_U8
#define GL_K_x64_U16  GL_K_U16
#define GL_K_x64_U32  GL_K_U32
#define GL_K_x64_I32  GL_K_I32
#define GL_K_x64_U64  GL_K_U64
#define GL_K_x64_V128 GL_K_V128
#define GL_K_host_U8   GL_K_U8
#define GL_K_host_U16  GL_K_U16
#define GL_K_host_U32  GL_K_U32
#define GL_K_host_I32  GL_K_I32
#define GL_K_host_U64  GL_K_U64
#define GL_K_host_V128 GL_K_V128

#define GL_FIELD_x86(name, kind)          GL_K_x86_##kind name;
#define GL_FIELDS_x86(name, kind, count)  GL_K_x86_##kind name[count];
#define GL_FIELD_x64(name, kind)          GL_K_x64_##kind name;
#define GL_FIELDS_x64(name, kind, count)  GL_K_x64_##kind name[count];
#define GL_FIELD_host(name, kind)         GL_K_host_##kind name;
#define GL_FIELDS_host(name, kind, count) GL_K_host_##kind name[count];

#define GL_STRUCT(S, abi) struct GL_##S##_##abi { GL_##S(GL_FIELD_##abi, GL_FIELDS_##abi) };

#endif
