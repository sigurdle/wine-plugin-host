/*
 * msvcrt's `__CxxFrameHandler`: the emulated CPU's side of host/wasm/cxxeh.c.
 *
 * The handler itself is C, ported from Wine (host/wasm/cxxeh.c names each function's source). This
 * file is what Wine's dlls/msvcrt/except_i386.c does in x86 assembly -- `call_handler`,
 * `continue_after_catch`, the `__CxxFrameHandler` entry that passes EAX on -- and so derives from it:
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
 * --- How a C++ exception reaches a catch block here ------------------------------------------
 *
 * A function with C++ EH registers a small stub of its own as its SEH handler:
 * `mov eax, offset FuncInfo; jmp [__CxxFrameHandler]`. `_CxxThrowException` raises 0xe06d7363, the
 * dispatch (win32/seh.js) enters that stub as it enters every guest handler, and the stub lands on
 * the shim's entry, which is {@link cxxFrameHandler}: the four handler arguments from the stack and
 * the descriptor from EAX go to the C.
 *
 * The C calls back for everything that is guest code or the emulated machine (the `eh*` imports of
 * host/hostfn.js, answered by {@link CXX_EH}):
 *  - an unwind funclet (a destructor), a catch block, a copy constructor and the exception
 *    object's destructor run in a nested `cpu.callGuest`, as win32/seh4.js runs filters;
 *  - Wine's `nested_frame` is taken from the guest stack, and its handler is the thunk
 *    {@link CXX_NESTED_HANDLER}, bound to the C's `cxxCatchNestedHandler`: the dispatch and
 *    RtlUnwind call it like any guest handler, which is how `throw;` inside a catch block finds
 *    the exception being handled, and how leaving a catch block by a throw destroys it.
 *
 * --- Leaving the catch block ---------------------------------------------------------------
 *
 * The catch block returns the address to continue at, and Wine's `continue_after_catch` loads ESP
 * from the frame ([frame-4]), EBP, and jumps. Here that is a THROW, {@link CatchContinuation}: the
 * C frames unwind, and the first {@link handle} on the way out whose `callGuest` level owns that
 * ESP sets the CPU there. Usually that is the same host call. It is an outer one when the catching
 * frame was not made in the innermost call: a catch block is itself a nested call, so
 * `catch (...) { throw; }` caught by a caller continues in the call the CALLER runs in, and every
 * call in between returns by this throw, as `ExceptTransfer` does for an except block.
 *
 * A CATCH FRAME ABOVE A HOST CALLBACK is reached the same way. A C++ exception thrown inside a
 * window procedure and caught by the function that called DispatchMessage crosses a `callGuest`
 * no handler entry is under; the host call that function made -- DispatchMessage itself -- enters
 * the continuation (x86/CPU.js `GuestTransfer`, `#doHostCall`), as Windows continues there:
 * tests/fixtures/cxxeh/callback_oracle.log. Across a callback the KERNEL makes, Windows ends the
 * process, with 0xC0000409 for a C++ throw (CPU.js `callGuest`).
 */

import { EAX, ECX, EBX, ESP, EBP, ESI, EDI, GuestTransfer } from "../x86/CPU.js";
import { THUNK_LIMIT } from "../x86/Memory.js";
import { rtlUnwind, SEH_NESTED_HANDLER } from "./seh.js";

/** The handler of a catch block's nested frame; beside the dispatcher's own two thunks. */
export const CXX_NESTED_HANDLER = SEH_NESTED_HANDLER + 4;

/** host/wasm/cxxeh.c `EH_CALL_*`. */
const EH_CALL_FUNCLET = 0, EH_CALL_THISCALL = 1;

const r32 = (cpu, a) => cpu.mem.read32(a >>> 0, cpu.eip) >>> 0;

/** What Windows 11 ends the process with when a C++ throw would unwind across a callback the kernel made. */
const STATUS_STACK_BUFFER_OVERRUN = 0xc0000409;

/** Wine's `continue_after_catch(frame, addr)`, to be made by the call that owns `esp`. */
export class CatchContinuation extends GuestTransfer
{
	constructor(esp, ebp, eip)
	{
		super("a C++ catch block continues at 0x" + (eip >>> 0).toString(16) + " with ESP 0x" + (esp >>> 0).toString(16)
			+ ", in a call nothing entered it from", esp, STATUS_STACK_BUFFER_OVERRUN);
		this.name = "CatchContinuation";
		this.esp = esp >>> 0; this.ebp = ebp >>> 0; this.eip = eip >>> 0;
	}

	enter(cpu)
	{
		cpu.regs[ESP] = this.esp;
		cpu.regs[EBP] = this.ebp;
		cpu.eip = this.eip;
		cpu.hostCallSetEip = true;
	}
}

/** What the C imports (host/hostfn.js `hostImports`): `cpu.cxxEh`, installed by the first handler call. */
const CXX_EH = {
	/**
	 * Wine's `call_handler`, `call_copy_ctor`, `call_dtor`: guest code, nested. EBX, ESI, EDI and
	 * EBP come back as they were, as `call_handler` pushes and pops them.
	 * @param {number} args a raw offset into the wasm memory (the C's stack), `argc` dwords
	 */
	callGuest(cpu, kind, func, reg, argc, args)
	{
		const r = cpu.regs, ebx = r[EBX], esi = r[ESI], edi = r[EDI], ebp = r[EBP];
		const list = argc ? Array.from(new Uint32Array(cpu.mem.wasm.buffer, args >>> 0, argc)) : [];
		if (kind === EH_CALL_FUNCLET) r[EBP] = reg >>> 0;
		else if (kind === EH_CALL_THISCALL) r[ECX] = reg >>> 0;
		try { return cpu.callGuest(func >>> 0, list) >>> 0; }
		finally { r[EBX] = ebx; r[ESI] = esi; r[EDI] = edi; r[EBP] = ebp; }
	},
	/** Room on the guest stack, below everything the handler's own calls push. */
	alloca(cpu, bytes)
	{
		const p = ((cpu.regs[ESP] >>> 0) - (bytes >>> 0)) & ~3;
		cpu.regs[ESP] = p;
		return p >>> 0;
	},
	rtlUnwind(cpu, target, rec) { rtlUnwind(cpu, target >>> 0, rec >>> 0); },
	continueAfterCatch(cpu, frame, addr) { throw new CatchContinuation(r32(cpu, frame - 4), frame + 12, addr); },
	terminate(cpu)
	{
		cpu.halted = true;
		throw new Error("the plugin called terminate() from C++ exception handling -- msvcrt ended the process");
	},
};

function install(cpu)
{
	cpu.cxxEh = CXX_EH;
	cpu.bindHostCall(CXX_NESTED_HANDLER, "msvcrt!catch_function_nested_handler",
		(c, [rec, frame, ctx]) => handle(c, (hw) => hw.cxxCatchNestedHandler(rec, frame, ctx, c.fsBase, CXX_NESTED_HANDLER)), 4, false);
}

/**
 * Run a handler in the C. A {@link CatchContinuation} whose ESP this call owns is entered here;
 * any other goes on up. The C stack pointer is put back either way (see `vtableCall`,
 * host/hostfn.js: a throw does not restore the global).
 * @returns {number|undefined} the disposition, or undefined when the CPU was sent to a continuation
 */
function handle(cpu, call)
{
	if (cpu.cxxEh === null) install(cpu);
	const hw = cpu.hostWasm, sp = hw.__stack_pointer, was = sp.value, esp = cpu.regs[ESP];
	try
	{
		const d = call(hw) >>> 0;
		cpu.regs[ESP] = esp;
		return d;
	}
	catch (e)
	{
		if (!(e instanceof CatchContinuation) || e.esp >= (cpu.innermostCallEsp() >>> 0)) throw e;
		e.enter(cpu);
		return undefined;
	}
	finally { sp.value = was; }
}

/**
 * `__CxxFrameHandler(rec, frame, context, dispatch)`, the function descriptor in EAX. Also
 * `__CxxFrameHandler2` and `3` (Wine's msvcrt.spec forwards both to it).
 */
export function cxxFrameHandler(cpu, rec, frame, ctx, dispatch)
{
	const descr = cpu.regs[EAX] >>> 0;
	return handle(cpu, (hw) => hw.cxxFrameHandler(rec, frame, ctx, dispatch, descr, cpu.fsBase, CXX_NESTED_HANDLER));
}
