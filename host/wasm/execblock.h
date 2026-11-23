/*
 * THE EXECUTOR BLOCK -- the live layout (briefs/review_np_2026-11/x64_pass.md R11; applied by #3117).
 * x86/Memory.js imports every offset from host/structLayout.js, which tools/gen-structlayout.mjs
 * generates from this file, and layout.h derives HOSTFN_DATA_BASE from the block map below: this
 * file is the one source of the numbers.
 *
 * The block is 8 KB. Its state part has REGION sizes for the widest engine -- 16 GPRs of 8 bytes,
 * 16 XMM of 16 -- while the slot width an engine uses is its own: the x86 engine keeps 4-byte
 * registers inside the 16 x 8 region, so its `regs` view and the recompiler's `CS_REGS_REL + 4 * r`
 * are unchanged. The lazy-flag operands and the fault, call and thread-block words have 64-bit
 * slots; the x86 engine uses their low half (the host is little-endian).
 *
 * THE ONE COPY (roadmap E2). The lazy flags (flagOp, flagCf and the three operands; the encoding is
 * x86/FlagKind.js's), DF, the x87 TOP, status and control words and the instruction pointer are
 * these words and nothing else: the JS CPU reads and writes them through views, compiled regions
 * load and store them, and no message carries them. `ip` is the thread's, so it is the last word
 * of the machine state.
 *
 *   [0, regionIp)                      the THREAD's machine state, copied as a block on a switch
 *   [regionIp, EXEC_STATE bytes)       the EXECUTOR's protocol words
 *   [EXEC_LINKS_REL, +2 KB)            the worker link map, 32 sets x 8 ways x 8 bytes
 *   [EXEC_CSTACK_REL, EXEC_BLOCK_BYTES) the C stack of one hostfn.wasm instance (5 KB)
 *
 * EXEC_COUNT blocks start at EXEC_BLOCKS, where the code-page table ends; the module's static data
 * (HOSTFN_DATA_BASE) starts where they end. layout.h includes this file at its end.
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

/* The block map. Literal numbers, so tools/gen-structlayout.mjs exports each EXEC_ one to JS. */
#define EXEC_BLOCKS      0x00090000u
#define EXEC_COUNT       32
#define EXEC_BLOCK_BYTES 0x2000u
#define EXEC_LINKS_REL   0x400u
#define EXEC_LINK_SETS   32
#define EXEC_LINK_WAYS   8
#define EXEC_LINKS_BYTES (EXEC_LINK_SETS * EXEC_LINK_WAYS * 8u)
/*
 * THE C STACKS (2-05, roadmap E3). Every hostfn.wasm instance spills into one block's tail,
 * [EXEC_CSTACK_REL, EXEC_BLOCK_BYTES), and nothing but that instance ever moves its pointer: an
 * executor's instance has its own block's tail; the KERNEL's has the tail of block EXEC_KERNEL,
 * which runs no guest thread, so a kernel C frame (one waiting on the app, one that upcalls)
 * never shares bytes with an executor's. guestcall.c checks the floor before every guest call.
 */
#define EXEC_CSTACK_REL  0xc00u
#define EXEC_KERNEL      31

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
	FIELDS(busy,         U64, BUSY_WORDS) \
	FIELD(icountTotal,   U64)
GL_STRUCT(EXEC_STATE, host)

_Static_assert(sizeof(struct GL_EXEC_STATE_host) <= EXEC_LINKS_REL, "the state words run into the link map");
_Static_assert(EXEC_LINKS_REL + EXEC_LINKS_BYTES < EXEC_BLOCK_BYTES, "no room for the C stack");
_Static_assert(EXEC_CSTACK_REL == EXEC_LINKS_REL + EXEC_LINKS_BYTES, "the C stack starts where the link map ends");
_Static_assert(EXEC_KERNEL == EXEC_COUNT - 1, "the kernel's C stack is the last block's");

#endif
