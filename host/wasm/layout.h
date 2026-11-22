/*
 * WHERE THINGS ARE IN THE ONE WASM MEMORY — the numbers every C file here has to agree on.
 *
 * ⚠ `x86/Memory.js` IS THE AUTHORITY and `tests/HostFn.test.js` holds this file to it. It used to
 * be a `#define GUEST_BASE` in each .c, which was two copies of one number and is now three with
 * `arena.c` — and the day it moves (it has moved twice: 0x10000 → 0x90000 for the code-page
 * table, 0x90000 → 0xb0000 for the per-executor state blocks, 0xb0000 → 0x40b0000 for the pixel
 * arena, and up 0x20000 with the 8 KB executor blocks of #3117) a file left behind would address the wrong bytes with no diagnostic at all.
 *
 * ⚠ AND `hostfn.wasm` IS A COMMITTED BUILD ARTIFACT: changing anything here means re-running
 * `build.sh`, whose `--max-memory` carries GUEST_BASE + the 2 GB guest ceiling.
 *
 *   [0, 0x10000)          the JIT link map, the machine-wide words, the guest heap's metadata
 *   [0x10000, 0x90000)    the code-page table — 512 KB, one byte per 4 KB of the 2 GB ceiling
 *   [EXEC_BLOCKS, HOSTFN_DATA_BASE)  EXEC_COUNT executor blocks of EXEC_BLOCK_BYTES, [0x90000, 0xd0000):
 *                         registers, x87, SSE, link map, C stack -- see execblock.h
 *   [HOSTFN_DATA_BASE, GDI_BASE)  this module's static data -- build.sh `--global-base`
 *   [GDI_BASE, ARENA_BASE)    the GDI tables — see gdi32.c
 *   [ARENA_BASE, GUEST_BASE)  the host pixel arena — see arena.c
 *   [GUEST_BASE, ...)     the guest's address 0 onwards
 */

#ifndef HOSTFN_LAYOUT_H
#define HOSTFN_LAYOUT_H

typedef int i32;
typedef unsigned int u32;

/**
 * The GDI tables — the device-context table, its save pool, the surface table and the OBJECT
 * table, with its font pool (all gdi32.c); the bitmap table as it follows. ⚠ Growing this region moves ARENA_BASE and GUEST_BASE with it, and
 * SO DOES ADDING A FIELD to any struct in it, because nothing here is padded to a convenient size
 * (gdi32.c says why at length). Both are a `build.sh` away and that is the whole cost.
 */
/**
 * Where this module's static data is linked (build.sh reads x86/Memory.js and passes it as
 * `--global-base`, then refuses a module whose data ends past the region). No C reads these: they
 * are here so the map above is whole and so GDI_BASE is derived the way Memory.js derives it.
 * The base is where the executor blocks end; execblock.h (included at the end of this file) has
 * the block map, and Memory.js takes the same numbers from it.
 */
#define HOSTFN_DATA_BASE  (EXEC_BLOCKS + EXEC_COUNT * EXEC_BLOCK_BYTES)
#define HOSTFN_DATA_BYTES 0x00020000u

#define GDI_BASE    (HOSTFN_DATA_BASE + HOSTFN_DATA_BYTES)
#define GDI_BYTES   0x00100000u						/* the DC, save, surface, object, font, PHOTOSHOP, DOOR, COM and WINDOW tables */

/**
 * ★ THE WINDOW TABLE — every window of the process as a struct-of-words, at the TOP of the GDI
 * region so that user32.c can find it from this file alone. 2048 slots (win32/windows.js
 * `SLOTS`) of WINDOW_WORDS words each; gdi32.c asserts its own tables end below it. What the
 * words are is user32.c's; win32/windows.js and win32/controls.js write them through accessors.
 */
#define WINDOW_COUNT       2048u
#define WINDOW_WORDS       24u
#define WINDOW_TABLE_BYTES (WINDOW_COUNT * WINDOW_WORDS * 4u)
#define WINDOW_TABLE       (GDI_BASE + GDI_BYTES - WINDOW_TABLE_BYTES)

/**
 * ★ THE COM OBJECT TABLE (com.c) — a head, then COM_COUNT objects of COM_WORDS words, directly
 * BELOW the window table. gdi32.c asserts its own tables end below THIS now. What the words are
 * is com.c's; the guest half of an object is eight bytes of a pool in guest memory.
 */
#define COM_COUNT       1024u
#define COM_WORDS       8u
#define COM_HEAD_BYTES  64u
#define COM_TABLE_BYTES (COM_HEAD_BYTES + COM_COUNT * COM_WORDS * 4u)
#define COM_TABLE       (WINDOW_TABLE - COM_TABLE_BYTES)

/**
 * ★ THE VTABLE DISPATCHER'S WORDS (vtabledispatch.c) and ★ THE PHOTOSHOP HOST'S STATE
 * (photoshop.c), directly below the COM table. gdi32.c asserts its own tables end below PS_TABLE.
 */
#define VTABLE_SLOT_COUNT     1024u
#define VTABLE_DISPATCH_BYTES 128u
#define VTABLE_DISPATCH_STATE (COM_TABLE - VTABLE_DISPATCH_BYTES)
#define PS_TABLE_BYTES        1280u
#define PS_TABLE              (VTABLE_DISPATCH_STATE - PS_TABLE_BYTES)

/**
 * ★ THE NAMED REGIONS OF THE GUEST'S ADDRESS SPACE (x86/Memory.js REGION_TABLE, written by
 * `addRegion`), directly below the Photoshop host's state — the lowest table here now, so gdi32.c
 * asserts its own tables end below IT. A head (+0 the lock, a spin; +4 the count), then entries
 * sorted by address: +0 addr, +4 length, +8 flags, +12 a NUL-padded name. An entry with
 * REGION_IMAGE set is a PE image's whole mapping, which is what addressspace.c reports as MEM_IMAGE;
 * one with REGION_MAPPED set is a view of a section (win32/fileMapping.js), reported as MEM_MAPPED.
 */
#define REGION_SLOTS       512u
#define REGION_ENTRY_BYTES 48u
#define REGION_HEAD_BYTES  16u
#define REGION_TABLE_BYTES (REGION_HEAD_BYTES + REGION_SLOTS * REGION_ENTRY_BYTES)
#define REGION_TABLE       (PS_TABLE - REGION_TABLE_BYTES)
#define REGION_IMAGE       0x100u
#define REGION_MAPPED      0x400u

/**
 * ★ THE AFTER EFFECTS HOST'S STATE (aftereffects.c), directly below the region table — the lowest
 * of these now, so gdi32.c asserts its own tables end below IT.
 *
 * Every AE effect shares one process, so this is POOLS — plugins, instances, calls — and the
 * process-wide PICA and handle tables; aftereffects.h sizes the pools and aftereffects.c asserts
 * they fit. It is a sibling of PS_TABLE and not part of it because the two protocols never share
 * a plugin.
 */
#define AE_TABLE_BYTES     0x4000u
#define AE_TABLE           (REGION_TABLE - AE_TABLE_BYTES)

/**
 * The guest's SIZE — its address space is [0, size) — and the PROCESSOR COUNT it is told, two words
 * beside the machine words (x86/Memory.js CS_GUEST_SIZE, CS_PROCESSORS, the authority). What
 * addressspace.c returns the address range and `GetSystemInfo` from.
 */
#define CS_GUEST_SIZE 0x0000f160u
#define CS_PROCESSORS 0x0000f164u
/*
 * The guest's CAP and the heap bytes the HOST owns (x86/Memory.js CS_GUEST_CAP, CS_HEAP_HOSTBYTES,
 * the authority): heap.c `guestLimit` is their sum, how far the guest may grow.
 */
#define CS_GUEST_CAP      0x0000f168u
#define CS_HEAP_HOSTBYTES 0x0000f16cu

/**
 * THE PROCESS CENSUS — live and peak counts every worker raises and anyone holding the memory reads
 * without a lock: what `GetGuiResources` returns and the app's Task Manager shows. ⚠ x86/Memory.js
 * `CS_CENSUS` is the authority and says what each word counts; `censusAdd` is its twin there.
 */
#define CS_CENSUS          0x0000f170u
#define CENSUS_GDI         0u
#define CENSUS_GDI_PEAK    4u
#define CENSUS_USER        8u

/* Move a census count by `delta`, and raise its peak — the word after it — to match. */
static inline void censusAdd(u32 which, int delta)
{
	u32 *live = (u32 *)(CS_CENSUS + which), *peak = live + 1;
	u32 now = __atomic_add_fetch(live, (u32)delta, __ATOMIC_SEQ_CST);
	u32 seen = __atomic_load_n(peak, __ATOMIC_SEQ_CST);
	while ((int)now > (int)seen && !__atomic_compare_exchange_n(peak, &seen, now, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) { }
}

/**
 * The guest's RESERVED PAGE (guest 0x10000..0x20000, x86/Memory.js THUNK_BASE — the page whose
 * addresses stand for imported functions, and which holds no code). Import thunks are handed out
 * from the bottom and stop at COM_VTABLES (Memory.js IMPORT_THUNK_LIMIT); SEH and the return
 * sentinels sit at the top, from 0x1fc00.
 * ⚠ x86/Memory.js IS THE AUTHORITY for all of these, AND HAS THE MAP OF THE WHOLE PAGE.
 *
 *   COM_VTABLES   the guest's copy of every COM vtable — a dword per slot, holding that slot's number
 *   COM_POOL      the guest halves of the COM objects, eight bytes each
 *   VTABLE_SLOTS  the numbers themselves: slot n of all the registered vtables is VTABLE_SLOTS + 4n
 *                 (vtabledispatch.c) — nothing is AT these addresses; they stand for C functions
 *   PS_SUITES     the Photoshop suites as the guest reads them: version, count, the procs' numbers
 */
#define COM_VTABLE_WORDS 256u
#define COM_VTABLES     0x0001a000u
#define COM_POOL        (COM_VTABLES + COM_VTABLE_WORDS * 4u)
#define VTABLE_SLOTS    0x0001d000u
#define PS_SUITES       0x0001e000u
#define PS_SUITES_BYTES 2048u

/**
 * The desktop CHROME — caption, small caption, border, dialog frame, sizing frame, edge — as
 * six words after a marker, written by win32/metrics.js `setChrome`; the defaults apply while
 * the marker is 0. `GetWindowRect` on a top-level window adds the frame to the client, and the
 * app may have set a different one.
 */
#define CS_CHROME   0x0000f0c0u

/** Where a window surface's and a DDB's pixels live, so compiled code can reach them. See arena.c. */
#define ARENA_BASE  (GDI_BASE + GDI_BYTES)
#define ARENA_BYTES 0x04000000u						/* 64 MiB — the owner's call, 2026-09-20 */

/**
 * Where the guest's address 0 sits. A guest address A is at wasm offset A + GUEST_BASE, exactly
 * as compiled regions address memory — and there is no bounds check there, for the reason
 * `emitAddr` gives: the base is the memarg offset, so a wild pointer runs off the end of the
 * memory and traps, or, once photoshop.c has grown the document extent above the guest, lands in
 * that. Everything below is the host's and the guest cannot reach it.
 *
 * ⚠ THE C HAS NO MEMARG TO HIDE IT IN: its `A + GUEST_BASE` is a u32 add, which WRAPS below the
 * guest for an A at or above 2^32 - GUEST_BASE. So C reaches a guest address only through guest.h
 * `guest()`, which bounds it by the guest's own size and faults past it.
 */
#define GUEST_BASE  (ARENA_BASE + ARENA_BYTES)

/**
 * The screen, as four words — width, height, work width, work height — written by x86/CPU.js's
 * `screen` setter. ⚠ x86/Memory.js `CS_SCREEN` is the authority, as it is for GUEST_BASE.
 */
#define CS_SCREEN   0x0000f0b0u

/**
 * The SYSTEM COLORS — `GetSysColor`'s table, 31 COLORREF words, stored by gdi32.c `sysColorsInit`.
 * ⚠ x86/Memory.js `CS_SYS_COLORS` is the authority, as it is for CS_SCREEN.
 */
#define CS_SYS_COLORS 0x0000f0e0u

/**
 * THE JIT'S CODE-PAGE TABLE — one byte per 4 KB page of the guest, nonzero where some compiled region
 * was translated from that page. ⚠ x86/Memory.js `CS_CODE_PAGES` and `CODE_PAGE_BITS` are the
 * authority. Read by hostimports.h `guestWritten`: a write the C makes into such a page has to drop
 * the translation, as every write the interpreter makes does.
 */
#define CODE_PAGES     0x00010000u
#define CODE_PAGE_BITS 12u

#define TRUE 1
#define FALSE 0
#define EXPORT(name) __attribute__((export_name(name)))

/**
 * A FAST PATH's result (host/hostfn.js HOST_FAST): one i64, bit 32 set = HANDLED and the low word
 * EAX, or 0 = DECLINED — the compiled call site then makes the ordinary import call, to the
 * JavaScript entry. kernel32.c's critical sections and TLS, gdi32.c's ExtTextOutA.
 */
typedef unsigned long long u64;
#define HANDLED(eax) ((1ull << 32) | (u64)(u32)(eax))
#define DECLINED 0ull

/* Last, once u32 and u64 exist: the executor block map HOSTFN_DATA_BASE is derived from. */
#include "execblock.h"

#endif
