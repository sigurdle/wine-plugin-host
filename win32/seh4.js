/*
 * msvcrt's `_except_handler4_common`: the SEH4 frame handler.
 *
 * Ported from Wine dlls/msvcrt/except_i386.c (`_except_handler4_common`, `msvcrt_local_unwind4`,
 * `get_scopetable_v4`):
 *
 * Copyright 2000 Jon Griffiths
 * Copyright 2002 Alexandre Julliard
 * Copyright 2005 Juan Lang
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/**
 * A guest built with /GS links a small `_except_handler4` stub that forwards to this with
 * `(&__security_cookie, __security_check_cookie, rec, frame, ctx, dispatcher)`. The frame is SEH3's
 * { prev, handler, scopetable, trylevel } at EBP-16, but the scope table pointer is XORed with the
 * module's security cookie, the table starts with a 16-byte header
 * { gsCookieOffset, gsCookieXor, ehCookieOffset, ehCookieXor } before SEH3's 12-byte entries, and the
 * outermost try level is -2, not -1. Otherwise it runs as `exceptHandler3` (win32/seh.js) runs SEH3:
 * the filters from the current level outward with the EXCEPTION_POINTERS at [frame-4]; EXECUTE_HANDLER
 * unwinds the frames below, runs the inner `__finally`s and enters the except block.
 *
 * The local cookies are validated as Windows' msvcrt does (Wine does not; #2335 measured it, see
 * tests/fixtures/oracle-2335): the GS cookie when the table has one (offset not -2), then the EH
 * cookie, each `*(ebp + offset) ^ (ebp + xor)` handed to the guest's `__security_check_cookie`
 * (fastcall, ECX), which reports a failure and ends the process itself. They are validated on entry,
 * on both paths; when unwinding, again after the `__finally`s if the frame had a try level to leave;
 * when a filter returns EXECUTE_HANDLER, again after the frame's own local unwind (which itself
 * checks nothing) and before the except block.
 *
 * WARNING: NOT DONE: destroying a C++ exception object an except block catches (`__DestructExceptionObject`),
 * as `exceptHandler3` does not either.
 */

import { ECX, EBP } from "../x86/CPU.js";
import { rtlUnwind, ExceptTransfer, EXCEPTION_UNWINDING, EXCEPTION_EXIT_UNWIND,
	ExceptionContinueExecution, ExceptionContinueSearch } from "./seh.js";

const TRYLEVEL_END4 = -2;
const r32 = (cpu, a) => cpu.mem.read32(a >>> 0, cpu.eip) >>> 0;
const w32 = (cpu, a, v) => cpu.mem.write32(a >>> 0, v >>> 0, cpu.eip);

/** The decoded scope table: the frame's pointer XOR the cookie. */
function scopeTable4(cpu, cookiePtr, reg) { return (r32(cpu, reg + 8) ^ r32(cpu, cookiePtr)) >>> 0; }

/** Call a filter or finally funclet: no arguments, the frame's EBP, the result in EAX. */
function callFunclet(cpu, addr, frameEbp)
{
	const saved = cpu.regs[EBP];
	cpu.regs[EBP] = frameEbp >>> 0;
	try { return cpu.callGuest(addr >>> 0, []) | 0; }
	finally { cpu.regs[EBP] = saved; }
}

function checkCookie(cpu, check, value)
{
	const saved = cpu.regs[ECX];
	cpu.regs[ECX] = value >>> 0;
	try { cpu.callGuest(check >>> 0, []); }
	finally { cpu.regs[ECX] = saved; }
}

function validateLocalCookies(cpu, check, table, frameEbp)
{
	if (!check) return;
	const gsOffset = r32(cpu, table) | 0;
	if (gsOffset !== TRYLEVEL_END4)
		checkCookie(cpu, check, r32(cpu, frameEbp + gsOffset) ^ ((frameEbp + (r32(cpu, table + 4) | 0)) >>> 0));
	checkCookie(cpu, check, r32(cpu, frameEbp + (r32(cpu, table + 8) | 0)) ^ ((frameEbp + (r32(cpu, table + 12) | 0)) >>> 0));
}

/** `_local_unwind4`: leave the try levels down to `stop` (exclusive), running each `__finally`. */
export function localUnwind4(cpu, cookiePtr, reg, stop)
{
	const entries = scopeTable4(cpu, cookiePtr, reg) + 16, frameEbp = (reg + 16) >>> 0;
	for (let level = r32(cpu, reg + 12) | 0, guard = 0; level !== TRYLEVEL_END4 && level !== stop && guard < 4096; guard++)
	{
		const entry = entries + level * 12;
		const enclosing = r32(cpu, entry) | 0;
		w32(cpu, reg + 12, enclosing);
		if (!r32(cpu, entry + 4)) callFunclet(cpu, r32(cpu, entry + 8), frameEbp);
		level = enclosing;
	}
}

/**
 * @param {number} ptrs room for an EXCEPTION_POINTERS { rec, ctx } the filters read through [reg-4]
 * @returns {number|'transferred'}
 */
export function exceptHandler4Common(cpu, cookiePtr, check, rec, reg, ctx, ptrs)
{
	cpu.df = 0;
	const table = scopeTable4(cpu, cookiePtr, reg), frameEbp = (reg + 16) >>> 0;
	validateLocalCookies(cpu, check, table, frameEbp);
	if (r32(cpu, rec + 4) & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
	{
		if ((r32(cpu, reg + 12) | 0) !== TRYLEVEL_END4)
		{
			localUnwind4(cpu, cookiePtr, reg, TRYLEVEL_END4);
			validateLocalCookies(cpu, check, table, frameEbp);
		}
		return ExceptionContinueSearch;
	}
	w32(cpu, ptrs, rec); w32(cpu, ptrs + 4, ctx);
	w32(cpu, reg - 4, ptrs);
	for (let level = r32(cpu, reg + 12) | 0, guard = 0; level !== TRYLEVEL_END4 && guard < 4096; guard++)
	{
		const entry = table + 16 + level * 12;
		const enclosing = r32(cpu, entry) | 0, filter = r32(cpu, entry + 4), handler = r32(cpu, entry + 8);
		if (filter)
		{
			const verdict = callFunclet(cpu, filter, frameEbp);
			if (verdict < 0) return ExceptionContinueExecution;
			if (verdict > 0)								// EXCEPTION_EXECUTE_HANDLER
			{
				rtlUnwind(cpu, reg, rec);
				if ((r32(cpu, reg + 12) | 0) !== level) localUnwind4(cpu, cookiePtr, reg, level);
				w32(cpu, reg + 12, enclosing);
				validateLocalCookies(cpu, check, table, frameEbp);
				const transfer = new ExceptTransfer(reg, frameEbp, handler);
				// The frame is the caller's (the guest stub that called us made none): from inside
				// this host call the jump is made here, as the shim's `_except_handler3` entry makes it.
				if ((reg >>> 0) >= (cpu.innermostCallEsp() >>> 0)) throw transfer;
				return transfer.enter(cpu);
			}
		}
		level = enclosing;
	}
	return ExceptionContinueSearch;
}
