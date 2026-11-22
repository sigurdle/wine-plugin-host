/*
 * The x64 unwinder's host-side interface (unwind_x64.c): the function-table lookup
 * (RtlLookupFunctionEntry's core) and the virtual unwind (RtlVirtualUnwind's core), over guest memory.
 *
 * Our own header, not a port. The register set is a HOST struct, deliberately not the guest's x64
 * CONTEXT: that layout comes with the guest typedefs (roadmap step E1), and an adapter between the
 * two is a later step. Every address the x64 ABI makes 64-bit (an image base, RIP, RSP, a saved
 * register's slot) is a u64 here; a guest address must still fit the guest, and one that does not
 * faults as any other out-of-guest access does.
 */

#ifndef HOSTFN_UNWIND_X64_H
#define HOSTFN_UNWIND_X64_H

#include "guest.h"

/** One XMM register, low quadword first, as M128A. */
typedef struct X64M128 { u64 low, high; } X64M128;

/**
 * The registers an unwind reads and writes. `gpr` is in the x64 encoding's order, which is also
 * UNWIND_CODE's OpInfo order and CONTEXT's: RAX RCX RDX RBX RSP RBP RSI RDI R8..R15.
 */
typedef struct X64UnwindRegs
{
	u64 gpr[16];
	u64 rip;
	X64M128 xmm[16];
} X64UnwindRegs;

#define X64_RSP 4

/**
 * KNONVOLATILE_CONTEXT_POINTERS, as guest addresses: where each restored register was read from.
 * An unwind writes only the entries of registers it restores; 0 means never set.
 */
typedef struct X64ContextPointers
{
	u64 integer[16];
	u64 floating[16];
} X64ContextPointers;

/** What RtlVirtualUnwind returns through its out-parameters. */
typedef struct X64UnwindResult
{
	u64 handler;          /* the language handler's address, or 0 */
	u64 handlerData;      /* the address of the handler's data (after the handler RVA), or 0 */
	u64 establisherFrame;
	u32 machFrameUnwound;
} X64UnwindResult;

#define X64_UNW_FLAG_NHANDLER  0u
#define X64_UNW_FLAG_EHANDLER  1u
#define X64_UNW_FLAG_UHANDLER  2u
#define X64_UNW_FLAG_CHAININFO 4u

#define X64_STATUS_SUCCESS            0u
#define X64_STATUS_BAD_FUNCTION_TABLE 0xc00000ffu

/**
 * The RUNTIME_FUNCTION covering `pc` in the image at `imageBase` (whose PE32+ headers are in guest
 * memory), or 0. Finding the image for a pc (RtlPcToFileHeader) is the caller's.
 */
u64 x64LookupFunctionEntry(u64 imageBase, u64 pc);

/**
 * Unwind one frame of `regs` in place. `function` is the RUNTIME_FUNCTION's address (0: a leaf),
 * `type` UNW_FLAG_EHANDLER or UNW_FLAG_UHANDLER (or 0), `ptrs` may be null.
 * Returns X64_STATUS_SUCCESS or X64_STATUS_BAD_FUNCTION_TABLE.
 */
u32 x64VirtualUnwind(u32 type, u64 imageBase, u64 pc, u64 function, X64UnwindRegs *regs,
                     X64UnwindResult *result, X64ContextPointers *ptrs);

#endif
