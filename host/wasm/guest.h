/*
 * GUEST ADDRESSES — what a pointer from emulated x86 code is here, and how to read through one.
 */

#ifndef HOSTFN_GUEST_H
#define HOSTFN_GUEST_H

#include "layout.h"

/**
 * A GUEST ADDRESS. Every pointer a function called by the guest receives is one, because the
 * caller is x86 code and its address 0 is GUEST_BASE here. It is a number until `guest()` makes
 * it a pointer, which is the one place the base is added.
 */
typedef u32 GPTR;

/*
 * An access that FAULTS, thrown as a GuestFault (host/hostfn.js `hostGuestFault`), which ends the
 * run as one thrown by a JavaScript entry does. `guest()` below raises it for an access outside
 * the guest. addressspace.c raises it too, for an uncommitted page INSIDE the guest — NULL, a
 * reserved page, a free heap span — which x86/Memory.js `#check` lets through: that bounds only
 * the guest's end, and the null page is ordinary guest memory in both tiers. `addr` is the first
 * byte and `bytes` how many the entry was about to touch.
 */
__attribute__((import_module("env"), import_name("guestFault"), noreturn)) void guestFault(u32 addr, u32 bytes);

/** The guest's size: its addresses are [0, size) (x86/Memory.js CS_GUEST_SIZE, the authority). */
static inline u32 guestSize(void) { return *(const u32 *)CS_GUEST_SIZE; }

/**
 * Do the `n` bytes at `p` lie inside the guest? For an entry that returns an address it cannot
 * reach with a FAILURE RETURN rather than a fault — a Win32 entry where Windows was observed to
 * (each such caller quotes the oracle line), a Photoshop suite whose result is an OSErr or NULL —
 * and for a caller that bounds a whole span once and then walks it. In 64 bits, because `p + n`
 * wraps in 32.
 */
static inline int inGuest(GPTR p, u64 n) { return (u64)p + n <= guestSize(); }

/**
 * ★ WHERE A GUEST ADDRESS BECOMES A HOST POINTER, AND SO WHERE IT IS BOUNDED: the `n` bytes at `p`
 * must lie inside the guest, or the calling entry FAULTS — as Windows raises an access violation
 * for the same access ("k CopyRect-dst: ACCESS VIOLATION (exit 0xc0000005)",
 * tmp/win32-oracle/guestwrap-2026-09-26.log, as for 55 of the 75 calls there), and as a JavaScript
 * entry's own read past the end throws x86/Memory.js `#check`'s GuestFault.
 *
 * ⚠ THE ADD ALONE WRAPS. `p + GUEST_BASE` is a u32 add, so a `p` at or above 2^32 - GUEST_BASE
 * lands BELOW the guest, in the host block: the heap's lock, the executor blocks, the GDI tables,
 * the pixel arena. Compiled code carries the base in the memarg offset and traps there instead; a
 * `p` in [size, 2^32 - GUEST_BASE) reaches the document extent photoshop.c grows above the guest.
 * The bound keeps both out.
 */
static inline void *guest(GPTR p, u64 n)
{
	if (!inGuest(p, n)) guestFault(p, n > 0xffffffffu ? 0xffffffffu : (u32)n);
	return (void *)(p + GUEST_BASE);
}

/* ⚠ BYTE-WISE, because x86 aligns nothing: an out-parameter or a struct may sit at any address. */
static inline u32 get32(GPTR p) { u32 v; __builtin_memcpy(&v, guest(p, 4), 4); return v; }
static inline void put32(GPTR p, u32 v) { __builtin_memcpy(guest(p, 4), &v, 4); }
static inline i32 get16(GPTR p) { short v; __builtin_memcpy(&v, guest(p, 2), 2); return v; }
static inline void put16(GPTR p, i32 v) { short s = (short)v; __builtin_memcpy(guest(p, 2), &s, 2); }
static inline u32 get8(GPTR p) { return *(const unsigned char *)guest(p, 1); }
static inline void put8(GPTR p, u32 v) { *(unsigned char *)guest(p, 1) = (unsigned char)v; }
/** An out-parameter the caller may have passed as NULL. */
static inline void out32(GPTR p, u32 v) { if (p) put32(p, v); }

/*
 * ─── The calling thread's last error ────────────────────────────────────────────────────────
 *
 * An entry declared `state: true` (host/hostfn.js HOST_FN), and every HOST_FAST one, is handed the
 * calling CPU's STATE BLOCK after its arguments. Its FS base word is the running thread's TEB on
 * whichever worker the thread runs, and the TEB's +0x34 is the last error, where win32/shim.js
 * `setLastError` puts it and the CRT reads `fs:[0x34]` directly. The TEB is 0 before one exists.
 * ⚠ x86/Memory.js CS_FS_BASE_REL and win32/TEB.js TEB_LAST_ERROR are the authority.
 */
#define CS_FS_BASE_REL  0x48u
#define TEB_LAST_ERROR  0x34u
/** The error Windows sets for an access to a buffer it caught rather than faulted on. */
#define ERROR_NOACCESS  998u

static inline u32 tebOf(u32 state) { return *(const u32 *)(state + CS_FS_BASE_REL); }

#endif
