/*
 * x64 exception unwinding: the function-table lookup and the virtual unwind.
 *
 * Ported from Wine (wine-mirror/wine at 97027f879ba9):
 *   dlls/ntdll/unwind.c (the __x86_64__ part)  find_function_info, RtlLookupFunctionEntry,
 *                                              RtlVirtualUnwind2, get_opcode_size,
 *                                              get_main_function_start, is_address_in_function,
 *                                              is_inside_epilog, interpret_epilog
 *
 * Copyright 1999, 2005, 2019, 2024 Alexandre Julliard
 * Copyright 2021 Martin Storsjo
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
 *
 * ---------------------------------------------------------------------------------------------
 *
 * WHERE THIS DIFFERS FROM WINE, AND WHY.
 *
 * Wine unwinds its own process: every structure and stack slot is a native pointer. Here the image,
 * its .pdata/.xdata and the stack are GUEST memory, so each is read by address and offset through
 * guest.h (a byte-wise read, since nothing guarantees alignment), and the register set is a host
 * struct (unwind_x64.h), not CONTEXT -- the guest CONTEXT's layout arrives with the guest typedefs,
 * and the adapter onto this struct is a later step. A context pointer is the guest address a
 * register was restored from, where Wine stores a native pointer.
 *
 * The lookup is given the image base: Wine's RtlLookupFunctionTable finds the module for the pc,
 * which is RtlPcToFileHeader's job and not built yet; dynamic function tables (RtlAddFunctionTable)
 * are not either. is_address_in_function looks the jump target up in the SAME image: a target in
 * another image then finds no entry, which gives Wine's answer (not this function: an epilog's tail
 * jump) because a function of another image never shares this one's main start.
 *
 * Wine's TRACE/FIXME are dropped; where Wine FIXMEs and carries on (an unknown code, a
 * PUSH_MACHFRAME that is chained or not last) this carries on the same way. The arm64ec branches
 * are Wine's own platform and have no meaning here.
 */

#include "unwind_x64.h"

/** A guest address held as an x64 value: it must fit the 32-bit guest, or it is out of the guest. */
static GPTR gaddr(u64 a, u32 n)
{
	if (a >> 32) guestFault(0xffffffffu, n);
	return (GPTR)a;
}

static u32 rd8(u64 a) { return get8(gaddr(a, 1)); }
static u32 rd16(u64 a) { unsigned short v; __builtin_memcpy(&v, guest(gaddr(a, 2), 2), 2); return v; }
static u32 rd32(u64 a) { return get32(gaddr(a, 4)); }
static u64 rd64(u64 a) { u64 v; __builtin_memcpy(&v, guest(gaddr(a, 8), 8), 8); return v; }

/* RUNTIME_FUNCTION: three RVAs. */
#define RF_BEGIN  0u
#define RF_END    4u
#define RF_UNWIND 8u
#define RF_SIZE   12u

/*
 * UNWIND_INFO: byte 0 is version:3 flags:5, byte 1 the prolog size, byte 2 the code count, byte 3
 * frame_reg:4 frame_offset:4, then the codes (two bytes each: the prolog offset, then code:4
 * info:4), padded to an even count, then the handler data -- a handler RVA, or a chained
 * RUNTIME_FUNCTION.
 */
#define UI_CODES 4u

#define UWOP_PUSH_NONVOL     0
#define UWOP_ALLOC_LARGE     1
#define UWOP_ALLOC_SMALL     2
#define UWOP_SET_FPREG       3
#define UWOP_SAVE_NONVOL     4
#define UWOP_SAVE_NONVOL_FAR 5
#define UWOP_EPILOG          6
#define UWOP_SAVE_XMM128     8
#define UWOP_SAVE_XMM128_FAR 9
#define UWOP_PUSH_MACHFRAME  10

static u64 find_function_info(u64 pc, u64 base, u64 func, u32 count);

/**
 * RtlLookupFunctionEntry's core: the exception directory of the PE32+ image at `imageBase`, searched
 * for `pc`. The directory is data directory 3 of the optional header, which sits 0x18 into the NT
 * headers; a PE32+ optional header (magic 0x20b) has its data directories at offset 112.
 */
u64 x64LookupFunctionEntry(u64 imageBase, u64 pc)
{
	u64 nt = imageBase + rd32(imageBase + 0x3c);
	if (rd32(nt) != 0x4550u || rd16(nt + 0x18) != 0x20bu) return 0;
	if (rd32(nt + 0x18 + 108) <= 3) return 0;	/* NumberOfRvaAndSizes */
	u32 rva = rd32(nt + 0x18 + 112 + 3 * 8), size = rd32(nt + 0x18 + 112 + 3 * 8 + 4);
	if (!rva || !size) return 0;
	return find_function_info(pc, imageBase, imageBase + rva, size / RF_SIZE);
}

static u32 get_opcode_size(u64 op);
static int is_inside_epilog(u64 pc, u64 base, u64 function);
static void interpret_epilog(u64 pc, X64UnwindRegs *context, X64ContextPointers *ctx_ptr);
static void set_int_reg(X64UnwindRegs *context, X64ContextPointers *ctx_ptr, u32 reg, u64 val);
static void set_float_reg(X64UnwindRegs *context, X64ContextPointers *ctx_ptr, u32 reg, u64 val);

/**
 * RtlVirtualUnwind2's core (dlls/ntdll/unwind.c). `function` is a RUNTIME_FUNCTION's guest
 * address, 0 for a leaf.
 */
u32 x64VirtualUnwind(u32 type, u64 base, u64 pc, u64 function, X64UnwindRegs *context,
                     X64UnwindResult *result, X64ContextPointers *ctx_ptr)
{
	u64 frame, off, info = 0, handler_data = 0;
	u32 i, prolog_offset = ~0u, chained = 0, mach_frame = 0;

	result->handler = 0;
	result->handlerData = 0;
	result->machFrameUnwound = 0;
	frame = result->establisherFrame = context->gpr[X64_RSP];

	if (!function)	/* leaf function */
	{
		context->rip = rd64(context->gpr[X64_RSP]);
		context->gpr[X64_RSP] += 8;
		return X64_STATUS_SUCCESS;
	}

	for (;;)
	{
		info = base + rd32(function + RF_UNWIND);
		u32 b0 = rd8(info), version = b0 & 7, flags = b0 >> 3;
		u32 prolog = rd8(info + 1), count = rd8(info + 2), b3 = rd8(info + 3);
		u32 frame_reg = b3 & 15, frame_offset = b3 >> 4;
		handler_data = info + UI_CODES + ((count + 1) & ~1u) * 2;

		if (version != 1 && version != 2) return X64_STATUS_BAD_FUNCTION_TABLE;

		if (frame_reg) frame = context->gpr[frame_reg] - frame_offset * 16;

		/* check if in prolog */
		u64 begin = base + rd32(function + RF_BEGIN);
		if (pc >= begin && pc < begin + prolog)
			prolog_offset = (u32)(pc - begin);
		else
		{
			prolog_offset = ~0u;
			/* Since Win10 1809 epilogue does not have a special treatment in case of zero opcode count. */
			if (!chained && count && is_inside_epilog(pc, base, function))
			{
				interpret_epilog(pc, context, ctx_ptr);
				result->establisherFrame = frame_reg ? context->gpr[X64_RSP] - 8 : frame;
				return X64_STATUS_SUCCESS;
			}
		}

		for (i = 0; i < count; i += get_opcode_size(info + UI_CODES + i * 2))
		{
			u64 op = info + UI_CODES + i * 2;
			u32 code = rd8(op + 1) & 15, opinfo = rd8(op + 1) >> 4;
			if (prolog_offset < rd8(op)) continue;	/* skip it */

			switch (code)
			{
			case UWOP_PUSH_NONVOL:	/* pushq %reg */
				set_int_reg(context, ctx_ptr, opinfo, context->gpr[X64_RSP]);
				context->gpr[X64_RSP] += 8;
				break;
			case UWOP_ALLOC_LARGE:	/* subq $nn,%rsp */
				if (opinfo) context->gpr[X64_RSP] += rd32(op + 2);
				else context->gpr[X64_RSP] += (u64)rd16(op + 2) * 8;
				break;
			case UWOP_ALLOC_SMALL:	/* subq $n,%rsp */
				context->gpr[X64_RSP] += (opinfo + 1) * 8;
				break;
			case UWOP_SET_FPREG:	/* leaq nn(%rsp),%framereg */
				context->gpr[X64_RSP] = result->establisherFrame = frame;
				break;
			case UWOP_SAVE_NONVOL:	/* movq %reg,n(%rsp) */
				off = frame + (u64)rd16(op + 2) * 8;
				set_int_reg(context, ctx_ptr, opinfo, off);
				break;
			case UWOP_SAVE_NONVOL_FAR:	/* movq %reg,nn(%rsp) */
				off = frame + rd32(op + 2);
				set_int_reg(context, ctx_ptr, opinfo, off);
				break;
			case UWOP_SAVE_XMM128:	/* movaps %xmmreg,n(%rsp) */
				off = frame + (u64)rd16(op + 2) * 16;
				set_float_reg(context, ctx_ptr, opinfo, off);
				break;
			case UWOP_SAVE_XMM128_FAR:	/* movaps %xmmreg,nn(%rsp) */
				off = frame + rd32(op + 2);
				set_float_reg(context, ctx_ptr, opinfo, off);
				break;
			case UWOP_PUSH_MACHFRAME:
				if (flags & X64_UNW_FLAG_CHAININFO) break;	/* Wine: FIXME, chained */
				if (i + get_opcode_size(op) < count) break;	/* Wine: FIXME, not the last opcode */
				if (opinfo) context->gpr[X64_RSP] += 8;
				context->rip = rd64(context->gpr[X64_RSP]);
				context->gpr[X64_RSP] = rd64(context->gpr[X64_RSP] + 24);
				mach_frame = 1;
				break;
			default:	/* UWOP_EPILOG (version 2: nothing to do), or Wine's FIXME */
				break;
			}
		}

		if (!(flags & X64_UNW_FLAG_CHAININFO)) break;
		function = handler_data;	/* restart with the chained info */
		chained = 1;
	}

	if (!mach_frame)
	{
		/* now pop return address */
		context->rip = rd64(context->gpr[X64_RSP]);
		context->gpr[X64_RSP] += 8;
	}
	result->machFrameUnwound = mach_frame;

	if (!((rd8(info) >> 3) & type)) return X64_STATUS_SUCCESS;	/* no matching handler */
	if (prolog_offset != ~0u) return X64_STATUS_SUCCESS;	/* inside prolog */

	result->handler = base + rd32(handler_data);
	result->handlerData = handler_data + 4;
	return X64_STATUS_SUCCESS;
}

/* ---- the test exports ---------------------------------------------------------------------- */

/*
 * INTERNAL, for tests/UnwindX64.test.js: no Win32 entry is built on these yet (that needs
 * RtlCaptureContext, the guest CONTEXT adapter and an x64 executor). The register set and the
 * result travel through guest memory in the host structs' own layout (unwind_x64.h).
 */
EXPORT("x64LookupFunctionEntryTest") u64 x64LookupFunctionEntryTest(u64 imageBase, u64 pc)
{
	return x64LookupFunctionEntry(imageBase, pc);
}

EXPORT("x64VirtualUnwindTest") u32 x64VirtualUnwindTest(u32 type, u64 imageBase, u64 pc, u64 function,
                                                         GPTR regs, GPTR result, GPTR ptrs)
{
	X64UnwindRegs r;
	X64UnwindResult res;
	X64ContextPointers p;
	__builtin_memcpy(&r, guest(regs, sizeof r), sizeof r);
	if (ptrs) __builtin_memcpy(&p, guest(ptrs, sizeof p), sizeof p);
	u32 status = x64VirtualUnwind(type, imageBase, pc, function, &r, &res, ptrs ? &p : 0);
	__builtin_memcpy(guest(regs, sizeof r), &r, sizeof r);
	__builtin_memcpy(guest(result, sizeof res), &res, sizeof res);
	if (ptrs) __builtin_memcpy(guest(ptrs, sizeof p), &p, sizeof p);
	return status;
}

/* ---- helpers ------------------------------------------------------------------------------- */

/** Wine's find_function_info: a binary search, then an indirect entry (UnwindData bit 0) followed. */
static u64 find_function_info(u64 pc, u64 base, u64 func, u32 count)
{
	int min = 0;
	int max = (int)count - 1;

	while (min <= max)
	{
		int pos = (min + max) / 2;
		u64 f = func + (u64)pos * RF_SIZE;
		if (pc < base + rd32(f + RF_BEGIN)) max = pos - 1;
		else if (pc >= base + rd32(f + RF_END)) min = pos + 1;
		else
		{
			while (rd32(f + RF_UNWIND) & 1)	/* follow chained entry */
				f = base + (rd32(f + RF_UNWIND) & ~1u);
			return f;
		}
	}
	return 0;
}

static void set_int_reg(X64UnwindRegs *context, X64ContextPointers *ctx_ptr, u32 reg, u64 val)
{
	context->gpr[reg] = rd64(val);
	if (ctx_ptr) ctx_ptr->integer[reg] = val;
}

static void set_float_reg(X64UnwindRegs *context, X64ContextPointers *ctx_ptr, u32 reg, u64 val)
{
	context->xmm[reg].low = rd64(val);
	context->xmm[reg].high = rd64(val + 8);
	if (ctx_ptr) ctx_ptr->floating[reg] = val;
}

/** The slots a code takes, from the code's own address. */
static u32 get_opcode_size(u64 op)
{
	switch (rd8(op + 1) & 15)
	{
	case UWOP_ALLOC_LARGE:
		return 2 + ((rd8(op + 1) >> 4) != 0);
	case UWOP_SAVE_NONVOL:
	case UWOP_SAVE_XMM128:
	case UWOP_EPILOG:
		return 2;
	case UWOP_SAVE_NONVOL_FAR:
	case UWOP_SAVE_XMM128_FAR:
		return 3;
	default:
		return 1;
	}
}

static u64 get_main_function_start(u64 f, u64 base)
{
	while (rd32(f + RF_UNWIND))
	{
		u64 info = base + rd32(f + RF_UNWIND);
		if (!((rd8(info) >> 3) & X64_UNW_FLAG_CHAININFO)) break;
		f = info + UI_CODES + ((rd8(info + 2) + 1) & ~1u) * 2;
	}
	return base + rd32(f + RF_BEGIN);
}

static int is_address_in_function(u64 pc, u64 base, u64 function)
{
	u64 f;
	if (pc - base >= rd32(function + RF_BEGIN) && pc - base < rd32(function + RF_END)) return 1;
	if (!(f = x64LookupFunctionEntry(base, pc))) return 0;
	return get_main_function_start(f, base) == get_main_function_start(function, base);
}

static int is_inside_epilog(u64 pc, u64 base, u64 function)
{
	/* add or lea must be the first instruction, and it must have a rex.W prefix */
	u32 p0 = rd8(pc);
	if ((p0 & 0xf8) == 0x48)
	{
		u32 p1 = rd8(pc + 1), p2 = rd8(pc + 2);
		switch (p1)
		{
		case 0x81:	/* add $nnnn,%rsp */
			if (p0 == 0x48 && p2 == 0xc4) { pc += 7; break; }
			return 0;
		case 0x83:	/* add $n,%rsp */
			if (p0 == 0x48 && p2 == 0xc4) { pc += 4; break; }
			return 0;
		case 0x8d:	/* lea n(reg),%rsp */
			if (p0 & 0x06) return 0;	/* rex.RX must be cleared */
			if (((p2 >> 3) & 7) != 4) return 0;	/* dest reg must be %rsp */
			if ((p2 & 7) == 4) return 0;	/* no SIB byte allowed */
			if ((p2 >> 6) == 1) { pc += 4; break; }	/* 8-bit offset */
			if ((p2 >> 6) == 2) { pc += 7; break; }	/* 32-bit offset */
			return 0;
		}
	}

	/* now check for various pop instructions */
	for (;;)
	{
		u32 rex = 0;
		if ((rd8(pc) & 0xf0) == 0x40) rex = rd8(pc++) & 0x0f;	/* rex prefix */

		switch (rd8(pc))
		{
		case 0x58: case 0x59: case 0x5a: case 0x5b:	/* pop %rax..%rbx / %r8..%r11 */
		case 0x5c: case 0x5d: case 0x5e: case 0x5f:	/* pop %rsp..%rdi / %r12..%r15 */
			pc++;
			continue;
		case 0xc2:	/* ret $nn */
		case 0xc3:	/* ret */
			return 1;
		case 0xe9:	/* jmp nnnn */
			pc += 5 + (u64)(long long)(i32)rd32(pc + 1);
			return !is_address_in_function(pc, base, function);
		case 0xeb:	/* jmp n */
			pc += 2 + (u64)(long long)(signed char)rd8(pc + 1);
			return !is_address_in_function(pc, base, function);
		case 0xf3:	/* rep; ret (for amd64 prediction bug) */
			return rd8(pc + 1) == 0xc3;
		case 0xff:	/* jmp */
			if (rex && rex != 8) return 0;
			if (rd8(pc + 1) == 0x25) return 1;
			return rex && ((rd8(pc + 1) >> 3) & 7) == 4;
		}
		return 0;
	}
}

/** Execute a function epilog, which must have been validated with is_inside_epilog(). */
static void interpret_epilog(u64 pc, X64UnwindRegs *context, X64ContextPointers *ctx_ptr)
{
	for (;;)
	{
		u32 rex = 0;
		if ((rd8(pc) & 0xf0) == 0x40) rex = rd8(pc++) & 0x0f;	/* rex prefix */

		u32 b = rd8(pc);
		switch (b)
		{
		case 0x58: case 0x59: case 0x5a: case 0x5b:	/* pop %rax..%rbx / %r8..%r11 */
		case 0x5c: case 0x5d: case 0x5e: case 0x5f:	/* pop %rsp..%rdi / %r12..%r15 */
			set_int_reg(context, ctx_ptr, b - 0x58 + (rex & 1) * 8, context->gpr[X64_RSP]);
			context->gpr[X64_RSP] += 8;
			pc++;
			continue;
		case 0x81:	/* add $nnnn,%rsp */
			context->gpr[X64_RSP] += (u64)(long long)(i32)rd32(pc + 2);
			pc += 2 + 4;
			continue;
		case 0x83:	/* add $n,%rsp */
			context->gpr[X64_RSP] += (u64)(long long)(signed char)rd8(pc + 2);
			pc += 3;
			continue;
		case 0x8d:
			if ((rd8(pc + 1) >> 6) == 1)	/* lea n(reg),%rsp */
			{
				context->gpr[X64_RSP] = context->gpr[(rd8(pc + 1) & 7) + (rex & 1) * 8] + (u64)(long long)(signed char)rd8(pc + 2);
				pc += 3;
			}
			else	/* lea nnnn(reg),%rsp */
			{
				context->gpr[X64_RSP] = context->gpr[(rd8(pc + 1) & 7) + (rex & 1) * 8] + (u64)(long long)(i32)rd32(pc + 2);
				pc += 2 + 4;
			}
			continue;
		case 0xc2:	/* ret $nn */
			context->rip = rd64(context->gpr[X64_RSP]);
			context->gpr[X64_RSP] += 8 + rd16(pc + 1);
			return;
		case 0xe9:	/* jmp nnnn */
		case 0xeb:	/* jmp n */
		case 0xc3:	/* ret */
		case 0xf3:	/* rep; ret */
		case 0xff:	/* jmp */
			context->rip = rd64(context->gpr[X64_RSP]);
			context->gpr[X64_RSP] += 8;
			return;
		}
		return;
	}
}
