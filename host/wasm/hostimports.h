/*
 * WHAT THIS MODULE LACKS, IT IMPORTS — host/hostfn.js `hostImports` answers every one of these.
 *
 * No libc, no heap, 1.5 KB of stack (build.sh): it can print nothing, it cannot go on after an
 * impossible state, and the guest's allocator is not here yet.
 */

#ifndef HOSTFN_HOSTIMPORTS_H
#define HOSTFN_HOSTIMPORTS_H

#include "guest.h"

/*
 * `say` writes a line about something a caller handles and nobody would otherwise learn of.
 * host/hostfn.js `SAY` carries the same numbers, and tests/HostFn.test.js holds the two together.
 */
#define SAY_NO_INTERFACE     1u		/* a = the class's name (a C string), b = the IID in guest memory */
#define SAY_NOT_IMPLEMENTED  2u		/* a = the vtable slot's number */
#define SAY_NO_CLASS         3u		/* a = the CLSID, b = the IID, both in guest memory */
#define SAY_NO_PIXEL_FORMAT  4u		/* a = the pixel-format GUID in guest memory */
#define SAY_RESAMPLE_EXTENT  5u		/* a = 1 for the 2-D form, b = six ints: args 11-14, height, width */
#define SAY_COLOR_INFO_SIZE  6u		/* a = the `infoSize` a plugin passed where this host reads 36 */
#define SAY_NO_PLATFORM_DATA 7u		/* a = the PF_PlatDataID an effect asked for and this host has no answer for */
__attribute__((import_module("env"), import_name("say"))) void say(u32 what, u32 a, u32 b);

/*
 * It cannot go on: thrown from the import, which unwinds as a trap would but NAMES the reason.
 * host/hostfn.js `FATAL` carries the same numbers, and tests/HostFn.test.js holds the two together.
 */
#define FATAL_UNALIGNED_INTERLOCKED 1u
#define FATAL_TABLE_FULL            2u		/* addr = how many entries a fixed table would have needed */
#define FATAL_NOT_IMPLEMENTED       3u		/* addr = the number of a vtable slot that must never be reached */
__attribute__((import_module("env"), import_name("fatal"), noreturn)) void fatal(u32 reason, u32 addr);


/*
 * The guest heap: its allocator is x86/Memory.js. The day it is C these are calls, not imports.
 * `heapAlloc` answers 0 for a request the heap cannot hold; `heapSize` answers 0 for an address
 * that is not a live block, which is how an address the guest made up is told from a block.
 * `heapAlloc`, `heapRealloc` and `heapFree` THROW, through the C frames that called them, the
 * GuestFault a free-list link past the guest's memory is (x86/Memory.js `#reach`), as `guest()`
 * does for an access outside the guest: none of them is called with a C lock held.
 */
__attribute__((import_module("env"), import_name("heapAlloc")))   GPTR heapAlloc(u32 bytes);
__attribute__((import_module("env"), import_name("heapRealloc"))) GPTR heapRealloc(GPTR p, u32 bytes);
__attribute__((import_module("env"), import_name("heapFree")))    void heapFree(GPTR p);
__attribute__((import_module("env"), import_name("heapSize")))    u32 heapSize(GPTR p);
__attribute__((import_module("env"), import_name("heapSpace")))   u32 heapSpace(void);

/*
 * `n` bytes at `p` that this module wrote landed in a page the JIT translated code from: JavaScript
 * drops those translations, as it does for every write the interpreter makes (x86/Memory.js
 * `noteBulkWrite`). Only `guestWritten` calls it.
 */
__attribute__((import_module("env"), import_name("codeWrite"))) void codeWrite(GPTR p, u32 n);

/*
 * ★ A CALLER-SIZED COPY INTO GUEST MEMORY IS A GUEST WRITE LIKE ANY OTHER. The bytes may be code —
 * `BlockMove` is the SDK's memmove, and a plugin generating code copies it with whatever it has —
 * and the recompiler may have translated what was there before. So such a write checks the
 * code-page table (layout.h) over the pages it covered, one load each, and only a hit leaves wasm.
 */
static inline void guestWritten(GPTR p, u32 n)
{
	if (!n) return;
	const unsigned char *pages = (const unsigned char *)CODE_PAGES;
	for (u32 pg = p >> CODE_PAGE_BITS, last = (p + n - 1u) >> CODE_PAGE_BITS; pg <= last; pg++)
		if (pages[pg]) { codeWrite(p, n); return; }
}

#endif
