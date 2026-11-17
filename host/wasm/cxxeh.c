/*
 * msvcrt C++ exception handling: `__CxxFrameHandler` for 32-bit x86.
 *
 * Ported from Wine (wine-mirror/wine at 9f8c4ae4d35c):
 *   dlls/msvcrt/except_i386.c  cxx_local_unwind, catch_function_nested_handler, call_catch_block,
 *                              check_noexcept, cxx_frame_handler
 *   dlls/msvcrt/except.c       find_catch_handler, _CreateFrameInfo, _FindAndUnlinkFrame,
 *                              _IsExceptionObjectToBeDestroyed, __DestructExceptionObject,
 *                              __CxxUnregisterExceptionObject
 *   dlls/msvcrt/cppexcept.h    get_this_pointer, find_caught_type, copy_exception, is_cxx_exception
 *                              and the function descriptor layouts
 *   dlls/msvcrt/cxx.h          the exception type layouts
 * (Wine's cppexcept.c was folded into except.c and cpp.c; `_CxxThrowException` is cpp.c's and is
 * three record parameters and a raise, which win32/cxxeh.js makes.)
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
 *
 * ---------------------------------------------------------------------------------------------
 *
 * WHERE THIS DIFFERS FROM WINE, AND WHY.
 *
 * Wine's handler is x86 code on the thread's own stack; this is wasm, and the thread is emulated.
 * So every structure is read through its guest address by offset, and four things Wine does in
 * place are imports (win32/cxxeh.js handles them):
 *  - a catch block, an unwind funclet, a copy constructor and a destructor are GUEST code:
 *    `ehCallGuest` runs one on the emulated CPU and returns its EAX (Wine's call_handler,
 *    call_copy_ctor, call_dtor);
 *  - the frame for exceptions inside a catch block (`catch_func_nested_frame`) is a C local in
 *    Wine; here it must be guest memory, because it is linked into the guest's SEH chain and the
 *    dispatcher reads it: `ehAlloca` takes it from the guest stack, where Wine's local is;
 *  - `RtlUnwind` is win32/seh.js's;
 *  - `continue_after_catch` (ESP from the frame, EBP, a jump) is `ehContinueAfterCatch`, which
 *    does not return: it unwinds this call as a JavaScript throw and the CPU is set where the
 *    frame's call is (win32/cxxeh.js `CatchContinuation`).
 * The nested frame's handler is a thunk the host binds to `cxxCatchNestedHandler` below.
 *
 * msvcrt's per-thread data (`msvcrt_get_thread_data()`) is four words of the thread's TEB, where
 * `rand`'s state and `errno` already are (win32/TEB.js TEB_CXX_*, the authority). NOT a static:
 * this module's static data is one copy for every thread.
 *
 * NOT DONE: `_set_se_translator` (the shim has no such entry, so no translator can be set: a
 * non-C++ exception goes straight to the catch(...) search, as Wine does with none installed), and
 * the WinRT reference counting in copy_exception and __DestructExceptionObject.
 */

#include "hostimports.h"

/* What kind of guest code `ehCallGuest` runs, and so which register `reg` is. */
#define EH_CALL_FUNCLET  0u		/* no arguments, EBP = reg: a catch block or an unwind funclet */
#define EH_CALL_THISCALL 1u		/* ECX = reg, the callee pops: a copy constructor or a destructor */
#define EH_CALL_CDECL    2u		/* a custom handler */
__attribute__((import_module("env"), import_name("ehCallGuest")))
u32 ehCallGuest(u32 kind, GPTR func, u32 reg, u32 argc, const u32 *args);
__attribute__((import_module("env"), import_name("ehAlloca"))) GPTR ehAlloca(u32 bytes);
__attribute__((import_module("env"), import_name("ehRtlUnwind"))) void ehRtlUnwind(GPTR target, GPTR rec);
__attribute__((import_module("env"), import_name("ehContinueAfterCatch"), noreturn))
void ehContinueAfterCatch(GPTR frame, GPTR addr);
__attribute__((import_module("env"), import_name("ehTerminate"), noreturn)) void ehTerminate(void);

#define CXX_FRAME_MAGIC_VC6 0x19930520u
#define CXX_FRAME_MAGIC_VC7 0x19930521u
#define CXX_FRAME_MAGIC_VC8 0x19930522u
#define CXX_EXCEPTION       0xe06d7363u
#define CXX_EXCEPTION_PARAMS 3u

#define EXCEPTION_UNWINDING   2u
#define EXCEPTION_EXIT_UNWIND 4u
#define ExceptionContinueSearch 1u

#define FUNC_DESCR_SYNCHRONOUS 1u	/* synchronous exceptions only (built with /EHs and /EHsc) */
#define FUNC_DESCR_NOEXCEPT    4u	/* noexcept function */

#define CLASS_IS_SIMPLE_TYPE         1u
#define CLASS_HAS_VIRTUAL_BASE_CLASS 4u
#define TYPE_FLAG_CONST      1u
#define TYPE_FLAG_VOLATILE   2u
#define TYPE_FLAG_REFERENCE  8u

/* EXCEPTION_RECORD (80 bytes) */
#define REC_CODE    0u
#define REC_FLAGS   4u
#define REC_NPARAMS 16u
#define REC_INFO    20u
#define REC_SIZE    80u

/* cxx_exception_frame: the registration { prev, handler }, then: */
#define FRAME_TRYLEVEL 8u
#define FRAME_EBP      12u		/* the ADDRESS of this field is the function's EBP */

/* cxx_function_descr */
#define DESCR_MAGIC          0u		/* the low 29 bits */
#define DESCR_UNWIND_COUNT   4u
#define DESCR_UNWIND_TABLE   8u
#define DESCR_TRYBLOCK_COUNT 12u
#define DESCR_TRYBLOCK       16u
#define DESCR_FLAGS          32u	/* when magic >= VC8 */
/* unwind_info { prev, handler } */
#define UNWIND_SIZE 8u
/* tryblock_info { start_level, end_level, catch_level, catchblock_count, catchblock } */
#define TRY_START       0u
#define TRY_END         4u
#define TRY_CATCH_COUNT 12u
#define TRY_CATCHBLOCK  16u
#define TRY_SIZE        20u
/* catchblock_info { flags, type_info, offset, handler } */
#define CATCH_FLAGS     0u
#define CATCH_TYPE_INFO 4u
#define CATCH_OFFSET    8u
#define CATCH_HANDLER   12u
#define CATCH_SIZE      16u

/* cxx_exception_type { flags, destructor, custom_handler, type_info_table } */
#define EXC_FLAGS          0u
#define EXC_DESTRUCTOR     4u
#define EXC_CUSTOM_HANDLER 8u
#define EXC_TABLE          12u
/* cxx_type_info { flags, type_info, offsets { this_offset, vbase_descr, vbase_offset }, size, copy_ctor } */
#define TYPE_FLAGS     0u
#define TYPE_TYPE_INFO 4u
#define TYPE_OFFSETS   8u
#define TYPE_SIZE      20u
#define TYPE_COPY_CTOR 24u
/* type_info { vtable, name, mangled[] } */
#define TI_MANGLED 8u

/* catch_func_nested_frame: the registration { prev, handler }, then: */
#define NESTED_HANDLER   4u
#define NESTED_CXX_FRAME 8u
#define NESTED_DESCR     12u
#define NESTED_TRYLEVEL  16u
#define NESTED_FRAME_INFO 20u	/* cxx_frame_info { frame_info { object, next }, rec, context } */
#define NESTED_SIZE      36u
#define FI_OBJECT  0u
#define FI_NEXT    4u
#define FI_REC     8u
#define FI_CONTEXT 12u

/* msvcrt's thread data, in the TEB (win32/TEB.js TEB_CXX_*). */
#define TEB_EXCEPTION_LIST       0u
#define TEB_CXX_EXC_RECORD       0xfe8u
#define TEB_CXX_CTX_RECORD       0xfecu
#define TEB_CXX_PROCESSING_THROW 0xff0u
#define TEB_CXX_FRAME_INFO_HEAD  0xff4u

/* The running thread: its TEB, and the thunk a nested frame's handler is (see the file comment). */
typedef struct { GPTR teb, nested_handler; } thread_data_t;

static u32 call_handler(GPTR func, GPTR ebp) { return ehCallGuest(EH_CALL_FUNCLET, func, ebp, 0, 0); }

/* cppexcept.h call_copy_ctor, call_dtor */
static void call_copy_ctor(GPTR func, GPTR this, GPTR src, u32 has_vbase)
{
	const u32 args[2] = { src, 1 };
	ehCallGuest(EH_CALL_THISCALL, func, this, has_vbase ? 2 : 1, args);
}
static void call_dtor(GPTR func, GPTR this) { ehCallGuest(EH_CALL_THISCALL, func, this, 0, 0); }

static int guest_strcmp(GPTR a, GPTR b)
{
	for (;; a++, b++)
	{
		u32 x = get8(a), y = get8(b);
		if (x != y) return x < y ? -1 : 1;
		if (!x) return 0;
	}
}

static void guest_memmove(GPTR dest, GPTR src, u32 n)
{
	if (!n) return;
	__builtin_memmove(guest(dest, n), guest(src, n), n);
	guestWritten(dest, n);
}

/* cppexcept.h is_cxx_exception */
static int is_cxx_exception(GPTR rec)
{
	if (get32(rec + REC_CODE) != CXX_EXCEPTION) return 0;
	if (get32(rec + REC_NPARAMS) != CXX_EXCEPTION_PARAMS) return 0;
	return get32(rec + REC_INFO) >= CXX_FRAME_MAGIC_VC6 && get32(rec + REC_INFO) <= CXX_FRAME_MAGIC_VC8;
}

/* cppexcept.h get_this_pointer: compute the this pointer for a base class of a given type */
static GPTR get_this_pointer(GPTR off, GPTR object)
{
	if (!object) return 0;
	i32 vbase_descr = (i32)get32(off + 4);
	if (vbase_descr >= 0)
	{
		/* move this ptr to vbase descriptor */
		object += (u32)vbase_descr;
		/* and fetch additional offset from vbase descriptor */
		GPTR offset_ptr = get32(object) + get32(off + 8);
		object += get32(offset_ptr);
	}
	return object + get32(off);
}

/* cppexcept.h find_caught_type: check if the exception type is caught by a given catch block, and return the type that matched */
static GPTR find_caught_type(GPTR exc_type, GPTR catch_ti, u32 catch_flags)
{
	GPTR table = get32(exc_type + EXC_TABLE);
	u32 count = get32(table);
	for (u32 i = 0; i < count; i++)
	{
		GPTR type = get32(table + 4 + i * 4);
		GPTR ti = get32(type + TYPE_TYPE_INFO);
		if (!catch_ti) return type;   /* catch(...) matches any type */
		if (catch_ti != ti)
		{
			if (guest_strcmp(catch_ti + TI_MANGLED, ti + TI_MANGLED)) continue;
		}
		/* type is the same, now check the flags */
		if ((get32(exc_type + EXC_FLAGS) & TYPE_FLAG_CONST) && !(catch_flags & TYPE_FLAG_CONST)) continue;
		if ((get32(exc_type + EXC_FLAGS) & TYPE_FLAG_VOLATILE) && !(catch_flags & TYPE_FLAG_VOLATILE)) continue;
		return type;  /* it matched */
	}
	return 0;
}

/* cppexcept.h copy_exception: copy the exception object where the catch block wants it */
static void copy_exception(GPTR object, GPTR dest, u32 catch_flags, GPTR type)
{
	u32 flags = get32(type + TYPE_FLAGS), size = get32(type + TYPE_SIZE);
	if (catch_flags & TYPE_FLAG_REFERENCE)
	{
		put32(dest, get_this_pointer(type + TYPE_OFFSETS, object));
	}
	else if (flags & CLASS_IS_SIMPLE_TYPE)
	{
		guest_memmove(dest, object, size);
		/* if it is a pointer, adjust it */
		if (size == 4) put32(dest, get_this_pointer(type + TYPE_OFFSETS, get32(dest)));
	}
	else  /* copy the object */
	{
		GPTR copy_ctor = get32(type + TYPE_COPY_CTOR);
		if (copy_ctor)
			call_copy_ctor(copy_ctor, dest, get_this_pointer(type + TYPE_OFFSETS, object), flags & CLASS_HAS_VIRTUAL_BASE_CLASS);
		else
			guest_memmove(dest, get_this_pointer(type + TYPE_OFFSETS, object), size);
	}
}

/* except.c find_catch_handler. `frame` is the function's EBP. */
static GPTR find_catch_handler(GPTR object, GPTR frame, GPTR tryblock, GPTR exc_type)
{
	GPTR catchblock = get32(tryblock + TRY_CATCHBLOCK);
	u32 count = get32(tryblock + TRY_CATCH_COUNT);
	for (u32 i = 0; i < count; i++, catchblock += CATCH_SIZE)
	{
		GPTR catch_ti = get32(catchblock + CATCH_TYPE_INFO);
		if (exc_type)
		{
			GPTR type = find_caught_type(exc_type, catch_ti, get32(catchblock + CATCH_FLAGS));
			if (!type) continue;
			if (catch_ti && get8(catch_ti + TI_MANGLED) && get32(catchblock + CATCH_OFFSET))
			{
				/* copy the exception to its destination on the stack */
				copy_exception(object, frame + get32(catchblock + CATCH_OFFSET), get32(catchblock + CATCH_FLAGS), type);
			}
		}
		else
		{
			/* no CXX_EXCEPTION only proceed with a catch(...) block*/
			if (catch_ti) continue;
		}
		return get32(catchblock + CATCH_HANDLER);
	}
	return 0;
}

/* except.c _CreateFrameInfo */
static void create_frame_info(const thread_data_t *data, GPTR fi, GPTR obj)
{
	put32(fi + FI_NEXT, get32(data->teb + TEB_CXX_FRAME_INFO_HEAD));
	put32(data->teb + TEB_CXX_FRAME_INFO_HEAD, fi);
	put32(fi + FI_OBJECT, obj);
}

/* except.c _FindAndUnlinkFrame. A frame that is not in the list is left alone ("native crashes in this case"). */
static void find_and_unlink_frame(const thread_data_t *data, GPTR fi)
{
	GPTR cur = get32(data->teb + TEB_CXX_FRAME_INFO_HEAD);
	if (cur == fi)
	{
		put32(data->teb + TEB_CXX_FRAME_INFO_HEAD, get32(cur + FI_NEXT));
		return;
	}
	for (; cur && get32(cur + FI_NEXT); cur = get32(cur + FI_NEXT))
	{
		if (get32(cur + FI_NEXT) == fi)
		{
			put32(cur + FI_NEXT, get32(fi + FI_NEXT));
			return;
		}
	}
}

/* except.c _IsExceptionObjectToBeDestroyed */
static int is_exception_object_to_be_destroyed(const thread_data_t *data, GPTR obj)
{
	for (GPTR cur = get32(data->teb + TEB_CXX_FRAME_INFO_HEAD); cur; cur = get32(cur + FI_NEXT))
		if (get32(cur + FI_OBJECT) == obj) return 0;
	return 1;
}

/* except.c __DestructExceptionObject */
static void destruct_exception_object(GPTR rec)
{
	if (!rec || !is_cxx_exception(rec)) return;
	GPTR info = get32(rec + REC_INFO + 8);
	if (!info) return;
	GPTR destructor = get32(info + EXC_DESTRUCTOR);
	if (destructor) call_dtor(destructor, get32(rec + REC_INFO + 4));
}

/* except.c __CxxUnregisterExceptionObject, for the cxx_frame_info at `fi` */
static void unregister_exception_object(const thread_data_t *data, GPTR fi, int in_use)
{
	if (get32(fi + FI_REC) == 0xffffffffu) return;

	find_and_unlink_frame(data, fi);
	GPTR exc = get32(data->teb + TEB_CXX_EXC_RECORD);
	if (get32(exc + REC_CODE) == CXX_EXCEPTION && !in_use
		&& is_exception_object_to_be_destroyed(data, get32(exc + REC_INFO + 4)))
		destruct_exception_object(exc);
	put32(data->teb + TEB_CXX_EXC_RECORD, get32(fi + FI_REC));
	put32(data->teb + TEB_CXX_CTX_RECORD, get32(fi + FI_CONTEXT));
}

/* except_i386.c cxx_local_unwind: unwind the local function up to a given trylevel */
static void cxx_local_unwind(GPTR frame, GPTR descr, i32 last_level)
{
	i32 trylevel = (i32)get32(frame + FRAME_TRYLEVEL);

	while (trylevel != last_level)
	{
		if (trylevel < 0 || (u32)trylevel >= get32(descr + DESCR_UNWIND_COUNT)) ehTerminate();
		GPTR entry = get32(descr + DESCR_UNWIND_TABLE) + (u32)trylevel * UNWIND_SIZE;
		GPTR handler = get32(entry + 4);
		if (handler) call_handler(handler, frame + FRAME_EBP);
		trylevel = (i32)get32(entry);
	}
	put32(frame + FRAME_TRYLEVEL, (u32)last_level);
}

static u32 cxx_frame_handler(const thread_data_t *data, GPTR rec, GPTR frame, GPTR context, GPTR dispatch,
                             GPTR descr, GPTR nested_frame);

/* A rethrow (`throw;`): the record becomes the one being handled. With none, Windows ends the process. */
static void copy_rethrown_record(const thread_data_t *data, GPTR rec)
{
	GPTR prev_rec = get32(data->teb + TEB_CXX_EXC_RECORD);
	if (!prev_rec) ehTerminate();
	guest_memmove(rec, prev_rec, REC_SIZE);
	put32(rec + REC_FLAGS, get32(rec + REC_FLAGS) & ~EXCEPTION_UNWINDING);
}

/* except_i386.c catch_function_nested_handler: handler for exceptions happening while calling a catch function */
static u32 catch_function_nested_handler(const thread_data_t *data, GPTR rec, GPTR nested_frame, GPTR context)
{
	if (get32(rec + REC_FLAGS) & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
	{
		unregister_exception_object(data, nested_frame + NESTED_FRAME_INFO, 0);
		return ExceptionContinueSearch;
	}

	if (get32(rec + REC_CODE) == CXX_EXCEPTION)
	{
		GPTR prev_rec = get32(data->teb + TEB_CXX_EXC_RECORD);

		if ((get32(rec + REC_INFO + 4) == 0 && get32(rec + REC_INFO + 8) == 0) ||
			(get32(prev_rec + REC_CODE) == CXX_EXCEPTION &&
			 get32(rec + REC_INFO + 4) == get32(prev_rec + REC_INFO + 4) &&
			 get32(rec + REC_INFO + 8) == get32(prev_rec + REC_INFO + 8)))
		{
			/* exception was rethrown */
			copy_rethrown_record(data, rec);
		}
	}

	return cxx_frame_handler(data, rec, get32(nested_frame + NESTED_CXX_FRAME), context, 0,
	                         get32(nested_frame + NESTED_DESCR), nested_frame);
}

/* except_i386.c call_catch_block: find and call the appropriate catch block for an exception */
static void call_catch_block(const thread_data_t *data, GPTR rec, GPTR context, GPTR frame, GPTR descr,
                             GPTR catch_frame, GPTR info)
{
	GPTR object = get32(rec + REC_INFO + 4);
	i32 trylevel = (i32)get32(frame + FRAME_TRYLEVEL);
	u32 save_esp = get32(frame - 4);
	GPTR processing = data->teb + TEB_CXX_PROCESSING_THROW;
	u32 count = get32(descr + DESCR_TRYBLOCK_COUNT);

	put32(processing, get32(processing) + 1);
	for (u32 i = 0; i < count; i++)
	{
		GPTR tryblock = get32(descr + DESCR_TRYBLOCK) + i * TRY_SIZE;
		i32 start_level = (i32)get32(tryblock + TRY_START), end_level = (i32)get32(tryblock + TRY_END);

		/* only handle try blocks inside current catch block */
		if (catch_frame && (i32)get32(catch_frame + NESTED_TRYLEVEL) > start_level) continue;

		if (trylevel < start_level) continue;
		if (trylevel > end_level) continue;

		GPTR handler = find_catch_handler(object, frame + FRAME_EBP, tryblock, info);
		if (!handler) continue;

		/* Wine's local `nested_frame`, on the guest stack. */
		GPTR nested_frame = ehAlloca(NESTED_SIZE);

		/* Add frame info here so exception is not freed inside RtlUnwind call */
		create_frame_info(data, nested_frame + NESTED_FRAME_INFO, object);

		/* unwind the stack */
		ehRtlUnwind(catch_frame ? catch_frame : frame, rec);
		cxx_local_unwind(frame, descr, start_level);
		put32(frame + FRAME_TRYLEVEL, (u32)(end_level + 1));

		put32(nested_frame + NESTED_FRAME_INFO + FI_REC, get32(data->teb + TEB_CXX_EXC_RECORD));
		put32(nested_frame + NESTED_FRAME_INFO + FI_CONTEXT, get32(data->teb + TEB_CXX_CTX_RECORD));
		put32(data->teb + TEB_CXX_EXC_RECORD, rec);
		put32(data->teb + TEB_CXX_CTX_RECORD, context);
		put32(processing, get32(processing) - 1);

		/* setup an exception block for nested exceptions */
		put32(nested_frame + NESTED_HANDLER, data->nested_handler);
		put32(nested_frame + NESTED_CXX_FRAME, frame);
		put32(nested_frame + NESTED_DESCR, descr);
		put32(nested_frame + NESTED_TRYLEVEL, (u32)(end_level + 1));

		/* __wine_push_frame, the catch block, __wine_pop_frame */
		put32(nested_frame, get32(data->teb + TEB_EXCEPTION_LIST));
		put32(data->teb + TEB_EXCEPTION_LIST, nested_frame);
		GPTR addr = call_handler(handler, frame + FRAME_EBP);
		put32(data->teb + TEB_EXCEPTION_LIST, get32(nested_frame));

		put32(frame - 4, save_esp);
		unregister_exception_object(data, nested_frame + NESTED_FRAME_INFO, 0);

		ehContinueAfterCatch(frame, addr);
	}
	put32(processing, get32(processing) - 1);
}

/* except_i386.c check_noexcept */
static void check_noexcept(GPTR rec, GPTR descr, int nested)
{
	if (!nested && get32(rec + REC_CODE) == CXX_EXCEPTION &&
		(get32(descr + DESCR_MAGIC) & 0x1fffffffu) >= CXX_FRAME_MAGIC_VC8 &&
		(get32(descr + DESCR_FLAGS) & FUNC_DESCR_NOEXCEPT))
		ehTerminate();		/* noexcept function propagating exception */
}

/* except_i386.c cxx_frame_handler: implementation of __CxxFrameHandler */
static u32 cxx_frame_handler(const thread_data_t *data, GPTR rec, GPTR frame, GPTR context, GPTR dispatch,
                             GPTR descr, GPTR nested_frame)
{
	GPTR exc_type;
	u32 magic = get32(descr + DESCR_MAGIC) & 0x1fffffffu;

	if (magic < CXX_FRAME_MAGIC_VC6 || magic > CXX_FRAME_MAGIC_VC8) return ExceptionContinueSearch;
	if (magic >= CXX_FRAME_MAGIC_VC8 &&
		(get32(descr + DESCR_FLAGS) & FUNC_DESCR_SYNCHRONOUS) &&
		(get32(rec + REC_CODE) != CXX_EXCEPTION))
		return ExceptionContinueSearch;  /* handle only c++ exceptions */

	if (get32(rec + REC_FLAGS) & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
	{
		if (get32(descr + DESCR_UNWIND_COUNT) && !nested_frame) cxx_local_unwind(frame, descr, -1);
		return ExceptionContinueSearch;
	}
	if (!get32(descr + DESCR_TRYBLOCK_COUNT))
	{
		check_noexcept(rec, descr, nested_frame != 0);
		return ExceptionContinueSearch;
	}

	if (get32(rec + REC_CODE) == CXX_EXCEPTION &&
		get32(rec + REC_INFO + 4) == 0 && get32(rec + REC_INFO + 8) == 0)
		copy_rethrown_record(data, rec);

	if (get32(rec + REC_CODE) == CXX_EXCEPTION)
	{
		exc_type = get32(rec + REC_INFO + 8);

		if (get32(rec + REC_INFO) > CXX_FRAME_MAGIC_VC8 && get32(exc_type + EXC_CUSTOM_HANDLER))
		{
			const u32 args[8] = { rec, frame, context, dispatch, descr,
				nested_frame ? get32(nested_frame + NESTED_TRYLEVEL) : 0, nested_frame, 0 };
			return ehCallGuest(EH_CALL_CDECL, get32(exc_type + EXC_CUSTOM_HANDLER), 0, 8, args);
		}
	}
	else
	{
		/* A C exception. NOT DONE: the se_translator; see the file comment. */
		exc_type = 0;
	}

	call_catch_block(data, rec, context, frame, descr, nested_frame, exc_type);
	check_noexcept(rec, descr, nested_frame != 0);
	return ExceptionContinueSearch;
}

/*
 * __CxxFrameHandler (MSVCRT.@), and __CxxFrameHandler2 and 3, which Wine's msvcrt.spec forwards to
 * it. `descr` is what the guest's stub left in EAX. `teb` is the running thread's and
 * `nested_handler` the thunk bound to `cxxCatchNestedHandler`.
 */
EXPORT("cxxFrameHandler")
u32 cxxFrameHandler(GPTR rec, GPTR frame, GPTR context, GPTR dispatch, GPTR descr, GPTR teb, GPTR nested_handler)
{
	const thread_data_t data = { teb, nested_handler };
	return cxx_frame_handler(&data, rec, frame, context, dispatch, descr, 0);
}

/* The handler of a `catch_func_nested_frame`, called by the SEH dispatch and by RtlUnwind. */
EXPORT("cxxCatchNestedHandler")
u32 cxxCatchNestedHandler(GPTR rec, GPTR frame, GPTR context, GPTR teb, GPTR nested_handler)
{
	const thread_data_t data = { teb, nested_handler };
	return catch_function_nested_handler(&data, rec, frame, context);
}
