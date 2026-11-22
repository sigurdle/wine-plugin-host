/*
 * THE EXECUTOR BLOCK AS DECIDED FOR x64 ROOM (briefs/review_np_2026-11/x64_pass.md R11) -- NOT YET
 * THE LIVE LAYOUT. x86/Memory.js CS_*_REL and EXEC_* are still the authority for the running code;
 * moving to this layout is a separate step (it moves EXEC_BLOCKS' end, HOSTFN_DATA_BASE, GDI_BASE,
 * ARENA_BASE and GUEST_BASE, and the offsets x86/Recompiler.js emits).
 *
 * DECIDED: the block is 8 KB (EXEC_BLOCK_BYTES_X64ROOM). Its state part has REGION sizes for the
 * widest engine -- 16 GPRs of 8 bytes, 16 XMM of 16 -- while the slot width an engine uses is its
 * own named size: the x86 engine keeps GPR_BYTES 4 inside the 16 x 8 region, so its `regs` view and
 * the recompiler's `CS_REGS_REL + 4 * r` stay as they are, and only the region bases move.
 * Every offset is derived from the field list below (tools/gen-structlayout.mjs generates them for
 * JS into host/structLayout.js, and guestlayout_check.h holds C to the same numbers); no literal.
 *
 *   [0, EXEC_STATE bytes)              the state words below
 *   [EXEC_LINKS_X64ROOM, +2 KB)        the worker link map, same 32 sets x 8 ways x 8 bytes
 *   [.. EXEC_BLOCK_BYTES_X64ROOM)      the C stack (5 KB, from 1.5 KB today)
 */

#ifndef HOSTFN_EXECBLOCK_H
#define HOSTFN_EXECBLOCK_H

#include "structlayout.h"

#define GPR_COUNT   16
#define GPR_SLOT    8
#define XMM_COUNT   16
#define X87_COUNT   8
#define CALL_SLOTS  5
#define BUSY_WORDS  2

#define GL_EXEC_STATE(FIELD, FIELDS) \
	FIELDS(gpr,          U64, GPR_COUNT) \
	FIELD(flagDst,       U64) \
	FIELD(flagSrc,       U64) \
	FIELD(flagRes,       U64) \
	FIELD(flagOp,        U32) \
	FIELD(flagCf,        U32) \
	FIELD(faultAddr,     U64) \
	FIELD(faultIp,       U64) \
	FIELD(faultMeta,     U32) \
	FIELD(icount,        U32) \
	FIELD(threadBlock,   U64) \
	FIELD(df,            U32) \
	FIELD(bail,          U32) \
	FIELDS(callRa,       U64, CALL_SLOTS) \
	FIELDS(callTarget,   U64, CALL_SLOTS) \
	FIELDS(x87,          U64, X87_COUNT) \
	FIELDS(x87Tags,      U8,  X87_COUNT) \
	FIELD(x87Top,        I32) \
	FIELD(x87Sw,         U32) \
	FIELD(x87Cw,         U32) \
	FIELD(mxcsr,         U32) \
	FIELD(eflagsUser,    U32) \
	FIELDS(xmm,          V128, XMM_COUNT) \
	FIELDS(x87Int,       U64, X87_COUNT) \
	FIELD(ip,            U64) \
	FIELD(regionIp,      U64) \
	FIELD(codeWrite,     U64) \
	FIELD(codeWriteLen,  U32) \
	FIELD(wake,          U32) \
	FIELDS(busy,         U64, BUSY_WORDS)
GL_STRUCT(EXEC_STATE, host)

#define EXEC_BLOCK_BYTES_X64ROOM 0x2000u
#define EXEC_LINKS_X64ROOM       0x400u
#define EXEC_LINKS_BYTES         (32u * 8u * 8u)

_Static_assert(sizeof(struct GL_EXEC_STATE_host) <= EXEC_LINKS_X64ROOM, "the state words run into the link map");
_Static_assert(EXEC_LINKS_X64ROOM + EXEC_LINKS_BYTES < EXEC_BLOCK_BYTES_X64ROOM, "no room for the C stack");

#endif
