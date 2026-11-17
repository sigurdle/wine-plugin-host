/*
 * kernel32's FormatMessageA and FormatMessageW: the message (the system table of winerror.c, or the
 * caller's string), its inserts formatted, its lines broken, into the caller's buffer or a LocalAlloc
 * block.
 *
 * Derived from Wine 11.0's dlls/kernelbase/locale.c (FormatMessageA, FormatMessageW, get_message) and
 * dlls/ntdll/rtlstr.c (RtlFormatMessageEx, add_format, get_arg):
 *
 * Copyright 1995 Martin von Loewis
 * Copyright 1998 David Lee Lambert
 * Copyright 2000 Julio Cesar Gazquez
 * Copyright 2003 Jon Griffiths
 * Copyright 2005 Dmitry Timoshkov
 * Copyright 2002, 2019 Alexandre Julliard
 * Copyright 1996-1998 Marcus Meissner
 * Copyright 2000 Alexandre Julliard
 * Copyright 2003 Thomas Mertes
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
 * WHERE THIS DIFFERS FROM WINE:
 * - FORMAT_MESSAGE_FROM_HMODULE is not read: a module's RT_MESSAGETABLE is not looked up, and the
 *   call fails with ERROR_RESOURCE_TYPE_NOT_FOUND whatever the other flags.
 * - The working buffers are guest heap blocks (heapAlloc), not the process heap; the text the
 *   caller gets is the same.
 * - An insert's printf conversion is this file's own (`fmt_print`, msvcrt's rules for the integer,
 *   character and string conversions). A floating-point conversion (e E f g G a A) or %n in an
 *   insert is not built and ends the run (FATAL_FORMAT_NOT_IMPLEMENTED), never a wrong text.
 * - The ANSI code page is 1252 (codepages.c), as GetACP returns here.
 */

#include "layout.h"
#include "guest.h"
#include "hostimports.h"
#include "codepages.h"

extern const u32 WINERR_COUNT;
extern const u32 WINERR_IDS[];
extern const unsigned short WINERR_OFFSETS[];
extern const char WINERR_TEXT[];

#define FORMAT_MESSAGE_ALLOCATE_BUFFER  0x0100u
#define FORMAT_MESSAGE_IGNORE_INSERTS   0x0200u
#define FORMAT_MESSAGE_FROM_STRING      0x0400u
#define FORMAT_MESSAGE_FROM_HMODULE     0x0800u
#define FORMAT_MESSAGE_FROM_SYSTEM      0x1000u
#define FORMAT_MESSAGE_ARGUMENT_ARRAY   0x2000u
#define FORMAT_MESSAGE_MAX_WIDTH_MASK   0x00ffu

#define ERROR_NOT_ENOUGH_MEMORY          8u
#define ERROR_INVALID_PARAMETER          87u
#define ERROR_INSUFFICIENT_BUFFER        122u
#define ERROR_MR_MID_NOT_FOUND           317u
#define ERROR_RESOURCE_TYPE_NOT_FOUND    1813u
#define ERROR_RESOURCE_LANG_NOT_FOUND    1815u
#define ERROR_NO_WORK_DONE               1235u

/* The NTSTATUS values the formatter returns, as the Win32 errors they become (set_ntstatus). */
#define ST_OK        0u
#define ST_OVERFLOW  1u
#define ST_INVALID   ERROR_INVALID_PARAMETER

static void set_error(u32 state, u32 code)
{
	const u32 teb = tebOf(state);
	if (teb) put32(teb + TEB_LAST_ERROR, code);
}

static inline u32 rd16(GPTR p) { return (u32)get16(p) & 0xffffu; }

/* --- The output: a guest block of `cap` WCHARs, filled to `pos` --------------------------- */

typedef struct { GPTR buf; u32 pos, cap; } Out;

static u32 add_chars(Out *o, const unsigned short *s, u32 n)
{
	if (n > o->cap - o->pos) return ST_OVERFLOW;
	for (u32 i = 0; i < n; i++) put16(o->buf + 2u * (o->pos + i), s[i]);
	o->pos += n;
	return ST_OK;
}

static u32 add_char(Out *o, u32 c) { unsigned short w = (unsigned short)c; return add_chars(o, &w, 1); }

/* --- An insert's printf: msvcrt's _snwprintf_s for one conversion ------------------------- */

/* Writes up to `room` characters at o->pos; past that, `over` is set and nothing more is written. */
typedef struct { Out *o; u32 n, room; int over; } Emit;

static void emit(Emit *e, u32 c)
{
	if (e->n >= e->room) { e->over = 1; return; }
	put16(e->o->buf + 2u * (e->o->pos + e->n), c);
	e->n++;
}
static void emit_pad(Emit *e, u32 c, i32 n) { while (n-- > 0) emit(e, c); }

/*
 * One conversion, `spec` the text after the '%' (flags, width, precision, size, type), its
 * arguments taken in order from `args` (a '*' first, as printf does). Returns the characters
 * written, or -1 where _snwprintf_s would, the output being longer than `room`.
 */
static i32 fmt_print(Out *o, u32 room, const char *spec, const u32 *args)
{
	Emit e = { o, 0, room, 0 };
	u32 ai = 0;
	int left = 0, plus = 0, space = 0, alt = 0, zero = 0;
	for (;; spec++)
	{
		if (*spec == '-') left = 1;
		else if (*spec == '+') plus = 1;
		else if (*spec == ' ') space = 1;
		else if (*spec == '#') alt = 1;
		else if (*spec == '0') zero = 1;
		else break;
	}
	i32 width = 0, prec = -1;
	if (*spec == '*') { width = (i32)args[ai++]; spec++; if (width < 0) { left = 1; width = -width; } }
	else while (*spec >= '0' && *spec <= '9') width = width * 10 + (*spec++ - '0');
	if (*spec == '.')
	{
		spec++;
		prec = 0;
		if (*spec == '*') { prec = (i32)args[ai++]; spec++; if (prec < 0) prec = -1; }
		else while (*spec >= '0' && *spec <= '9') prec = prec * 10 + (*spec++ - '0');
	}
	/* Size: h, l, ll, w, I, I32, I64. In a wide printf `s`/`c` are wide and `S`/`C` narrow; h makes either narrow, l or w wide. */
	int is64 = 0, narrow = -1;
	for (;;)
	{
		if (*spec == 'h') { narrow = 1; spec++; }
		else if (*spec == 'l' && spec[1] == 'l') { is64 = 1; spec += 2; }
		else if (*spec == 'l' || *spec == 'w') { narrow = 0; spec++; }
		else if (*spec == 'I' && spec[1] == '6' && spec[2] == '4') { is64 = 1; spec += 3; }
		else if (*spec == 'I' && spec[1] == '3' && spec[2] == '2') spec += 3;
		else if (*spec == 'I') spec++;
		else break;
	}
	const char type = *spec;

	if (type == 's' || type == 'S' || type == 'c' || type == 'C')
	{
		const int isNarrow = narrow >= 0 ? narrow : (type == 'S' || type == 'C');
		const u32 v = args[ai];
		if (type == 'c' || type == 'C')
		{
			const u32 ch = isNarrow ? codePageDecode(1252u, v & 0xffu) : (v & 0xffffu);
			if (!left) emit_pad(&e, ' ', width - 1);
			emit(&e, ch);
			if (left) emit_pad(&e, ' ', width - 1);
		}
		else
		{
			static const char nul[] = "(null)";
			u32 len = 0;
			const u32 max = prec < 0 ? 0xffffffffu : (u32)prec;
			if (v) while (len < max && (isNarrow ? get8(v + len) : rd16(v + 2u * len))) len++;
			else while (len < max && nul[len]) len++;
			if (!left) emit_pad(&e, ' ', width - (i32)len);
			for (u32 i = 0; i < len; i++)
				emit(&e, !v ? (u32)nul[i] : isNarrow ? codePageDecode(1252u, get8(v + i)) : rd16(v + 2u * i));
			if (left) emit_pad(&e, ' ', width - (i32)len);
		}
	}
	else if (type == 'd' || type == 'i' || type == 'u' || type == 'x' || type == 'X' || type == 'o' || type == 'p')
	{
		u64 v = args[ai];
		if (is64) v |= (u64)args[ai + 1] << 32;
		int neg = 0;
		if (type == 'd' || type == 'i')
		{
			if (is64 ? (long long)v < 0 : (i32)v < 0) { neg = 1; v = is64 ? (u64)-(long long)v : (u64)(0u - (u32)v); }
		}
		const char *digits = type == 'X' || type == 'p' ? "0123456789ABCDEF" : "0123456789abcdef";
		const u32 base = type == 'o' ? 8u : type == 'x' || type == 'X' || type == 'p' ? 16u : 10u;
		if (type == 'p') { prec = 8; alt = 0; }
		char num[32];
		i32 n = 0;
		for (u64 t = v; t; t /= base) num[n++] = digits[t % base];
		char pre[2];
		i32 npre = 0;
		if (neg) pre[npre++] = '-';
		else if (plus && (type == 'd' || type == 'i')) pre[npre++] = '+';
		else if (space && (type == 'd' || type == 'i')) pre[npre++] = ' ';
		if (alt && v && base == 16u) { pre[npre++] = '0'; pre[npre++] = type == 'X' ? 'X' : 'x'; }
		i32 zeros = prec >= 0 ? (prec > n ? prec - n : 0) : 0;
		if (alt && base == 8u && zeros == 0 && (n == 0 || num[n - 1] != '0')) zeros = 1;
		if (prec < 0 && n == 0) zeros = 1;		/* 0 prints "0"; ".0" with 0 prints nothing */
		i32 total = npre + zeros + n;
		if (zero && !left && prec < 0 && width > total) { zeros += width - total; total = width; }
		if (!left) emit_pad(&e, ' ', width - total);
		for (i32 i = 0; i < npre; i++) emit(&e, (u32)pre[i]);
		emit_pad(&e, '0', zeros);
		while (n > 0) emit(&e, (u32)num[--n]);
		if (left) emit_pad(&e, ' ', width - total);
	}
	else fatal(FATAL_FORMAT_NOT_IMPLEMENTED, (u32)(unsigned char)type);

	return e.over ? -1 : (i32)e.n;
}

/* --- RtlFormatMessageEx ------------------------------------------------------------------- */

typedef struct
{
	i32 last;		/* last used arg */
	GPTR array;		/* args array, or 0 */
	GPTR list;		/* where the va_list's next argument is, or 0 */
	int hasList;
	u64 arglist[102];	/* arguments fetched from the va_list */
} Args;

static u64 get_arg(i32 nr, Args *a, int is64)
{
	if (nr == -1) nr = a->last + 1;
	while (nr > a->last)
	{
		u64 v = get32(a->list);
		if (is64) { v |= (u64)get32(a->list + 4u) << 32; a->list += 8u; }
		else a->list += 4u;
		a->arglist[a->last++] = v;
	}
	return a->arglist[nr - 1];
}

static int in_set(u32 c, const char *set) { for (; *set; set++) if ((u32)(unsigned char)*set == c) return 1; return 0; }

static u32 add_format(Out *o, GPTR *src, i32 insert, int ansi, Args *a)
{
	GPTR format = *src;
	char fmt[32];
	u32 args[5] = { 0, 0, 0, 0, 0 };
	int is64 = 0;
	u32 p = 0, stars = 0, nb = 0;

	if (rd16(format) == '!')
	{
		format += 2u;
		GPTR end = format;
		while (rd16(end) && rd16(end) != '!') end += 2u;
		if (!rd16(end) || (end - format) / 2u > sizeof(fmt) - 2u) return ST_INVALID;
		*src = end + 2u;
		while (in_set(rd16(format), "0123456789 +-*#."))
		{
			if (rd16(format) == '*') stars++;
			fmt[p++] = (char)rd16(format);
			format += 2u;
		}
		if (stars > 2u) return ST_INVALID;
		const u32 c = rd16(format);
		if (c == 'c' || c == 'C' || c == 's' || c == 'S')
		{
			if (ansi) { fmt[p++] = (char)(c ^ ('s' - 'S')); format += 2u; }
		}
		else if (c == 'I' && rd16(format + 2u) == '6' && rd16(format + 4u) == '4') is64 = 1;
		while (format != end) { const u32 ch = rd16(format); fmt[p++] = ch < 0x80u ? (char)ch : '?'; format += 2u; }
	}
	else fmt[p++] = ansi ? 'S' : 's';
	fmt[p] = 0;

	if (a->hasList)
	{
		get_arg(insert - 1, a, is64);		/* make sure previous args have been fetched */
		while (stars--)
		{
			args[nb++] = (u32)get_arg(insert, a, 0);
			insert = -1;
		}
		/* replicate MS bug: drop an argument when using va_list with width/precision */
		if (insert == -1) a->last--;
		const u64 val = get_arg(insert, a, is64);
		args[nb++] = (u32)val;
		args[nb] = (u32)(val >> 32);
	}
	else if (a->array)
	{
		args[nb++] = get32(a->array + 4u * (u32)(insert - 1));
		if (a->last < insert) a->last = insert;
		/* replicate MS bug: first arg is considered 64-bit, even if it's actually width or precision */
		if (is64) nb++;
		while (stars--) args[nb++] = get32(a->array + 4u * (u32)(a->last++));
	}
	else return ST_INVALID;

	const u32 room = o->cap - o->pos;
	if (!room) return ST_OVERFLOW;
	const i32 len = fmt_print(o, room - 1u, fmt, args);
	if (len == -1) return ST_OVERFLOW;
	o->pos += (u32)len;
	return ST_OK;
}

/* `src` a NUL-terminated WCHAR string in guest memory. */
static u32 format_message(GPTR src, u32 width, int ignore_inserts, int ansi, int is_array, GPTR args, int hasList,
                          Out *o, GPTR *listAfter)
{
	static const unsigned short crlf[2] = { '\r', '\n' };
	Args a;
	a.last = 0;
	a.array = is_array ? args : 0u;
	a.hasList = !is_array && hasList;
	a.list = a.hasList ? args : 0u;
	u32 status = ST_OK;
	u32 line = 0;
	u32 space = 0;
	int hasSpace = 0;

	for (; rd16(src); src += 2u)
	{
		u32 c = rd16(src);
		switch (c)
		{
		case '\r':
			if (rd16(src + 2u) == '\n') src += 2u;
			/* fall through */
		case '\n':
			if (!width)
			{
				status = add_chars(o, crlf, 2);
				line = o->pos;
				hasSpace = 0;
				break;
			}
			/* fall through */
		case ' ':
			space = o->pos; hasSpace = 1;
			status = add_char(o, ' ');
			break;
		case '\t':
			if (hasSpace && space == o->pos - 1u) space = o->pos;
			status = add_char(o, '\t');
			break;
		case '%':
			src += 2u;
			c = rd16(src);
			switch (c)
			{
			case 0:
				return ST_INVALID;
			case 't':
				if (!width)
				{
					status = add_char(o, '\t');
					break;
				}
				/* fall through */
			case 'n':
				status = add_chars(o, crlf, 2);
				line = o->pos;
				hasSpace = 0;
				break;
			case 'r':
				status = add_char(o, '\r');
				line = o->pos;
				hasSpace = 0;
				break;
			case '0':
				while (rd16(src + 2u)) src += 2u;
				break;
			case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
				if (!ignore_inserts)
				{
					i32 nr = (i32)(c - '0');
					src += 2u;
					if (rd16(src) >= '0' && rd16(src) <= '9') { nr = nr * 10 + (i32)(rd16(src) - '0'); src += 2u; }
					status = add_format(o, &src, nr, ansi, &a);
					src -= 2u;
					break;
				}
				/* fall through */
			default:
				if (ignore_inserts) { const unsigned short two[2] = { '%', (unsigned short)c }; status = add_chars(o, two, 2); }
				else status = add_char(o, c);
				break;
			}
			break;
		default:
			status = add_char(o, c);
			break;
		}

		if (status) return status;

		if (width && o->pos - line >= width)
		{
			i32 diff = 2;
			u32 next;
			if (hasSpace)		/* split line at the last space */
			{
				next = space + 1u;
				while (space > line && (rd16(o->buf + 2u * (space - 1u)) == ' ' || rd16(o->buf + 2u * (space - 1u)) == '\t')) space--;
				diff -= (i32)(next - space);
			}
			else space = next = o->pos;		/* split at the end of the buffer */

			if (diff > 0 && o->cap - o->pos < (u32)diff) return ST_OVERFLOW;
			const u32 tail = o->pos - next;
			if (tail) __builtin_memmove(guest(o->buf + 2u * (space + 2u), 2u * tail), guest(o->buf + 2u * next, 2u * tail), 2u * tail);
			o->pos = (u32)((i32)o->pos + diff);
			add_chars(&(Out){ o->buf, space, o->cap }, crlf, 2);
			line = space + 2u;
			hasSpace = 0;
		}
	}

	if ((status = add_char(o, 0))) return status;
	if (listAfter) *listAfter = a.list;
	return ST_OK;
}

/* --- get_message -------------------------------------------------------------------------- */

/*
 * The system table's text of `id` as a WCHAR string in a new guest block (its "\n" become "\r\n"),
 * or 0 with the error in *err. The table is English only: LANG_NEUTRAL and LANG_ENGLISH (any
 * sublanguage, 0 and the user/system defaults among them) find it; another language does not.
 */
static GPTR system_message(u32 id, u32 lang, u32 *err)
{
	/* Fold win32 hresult to its embedded error code. */
	if ((id >> 31) == 1u && ((id >> 16) & 0x1fffu) == 7u) id &= 0xffffu;
	const u32 primary = lang & 0x3ffu;
	if (primary != 0u && primary != 9u) { *err = ERROR_RESOURCE_LANG_NOT_FOUND; return 0; }
	u32 lo = 0, hi = WINERR_COUNT;
	while (lo < hi) { const u32 mid = (lo + hi) / 2u; if (WINERR_IDS[mid] < id) lo = mid + 1u; else hi = mid; }
	if (lo == WINERR_COUNT || WINERR_IDS[lo] != id) { *err = ERROR_MR_MID_NOT_FOUND; return 0; }
	const char *text = WINERR_TEXT + WINERR_OFFSETS[lo];
	u32 n = 0, lines = 0;
	for (; text[n]; n++) if (text[n] == '\n') lines++;
	const GPTR w = heapAlloc(2u * (n + lines + 1u));
	if (!w) { *err = ERROR_NOT_ENOUGH_MEMORY; return 0; }
	u32 k = 0;
	for (u32 i = 0; i <= n; i++)
	{
		if (text[i] == '\n') put16(w + 2u * k++, '\r');
		put16(w + 2u * k++, (unsigned char)text[i]);
	}
	return w;
}

/* The caller's string as a WCHAR string in a new guest block: an ANSI one decoded from code page 1252. */
static GPTR string_message(GPTR s, int ansi, u32 *err)
{
	u32 n = 0;
	if (ansi) while (get8(s + n)) n++;
	else while (rd16(s + 2u * n)) n++;
	const GPTR w = heapAlloc(2u * (n + 1u));
	if (!w) { *err = ERROR_NOT_ENOUGH_MEMORY; return 0; }
	for (u32 i = 0; i <= n; i++) put16(w + 2u * i, ansi ? codePageDecode(1252u, get8(s + i)) : rd16(s + 2u * i));
	return w;
}

static GPTR get_message(u32 flags, GPTR source, u32 id, u32 lang, int ansi, u32 *err)
{
	if (flags & FORMAT_MESSAGE_FROM_STRING) return string_message(source, ansi, err);
	if (flags & FORMAT_MESSAGE_FROM_HMODULE) { *err = ERROR_RESOURCE_TYPE_NOT_FOUND; return 0; }
	if (flags & FORMAT_MESSAGE_FROM_SYSTEM) return system_message(id, lang, err);
	*err = ERROR_INVALID_PARAMETER;
	return 0;
}

static void zero_guest(GPTR p, u32 n) { __builtin_memset(guest(p, n), 0, n); }

/* --- The entries -------------------------------------------------------------------------- */

/* `args` is a `va_list *` (a pointer to the word holding the next argument's address) or, with ARGUMENT_ARRAY, the array. */
static GPTR list_of(u32 flags, GPTR args) { return args && !(flags & FORMAT_MESSAGE_ARGUMENT_ARRAY) ? get32(args) : 0u; }

EXPORT("FormatMessageA") u32 FormatMessageA(u32 flags, GPTR source, u32 msgid, u32 langid, GPTR buffer, u32 size, GPTR args, u32 state)
{
	u32 width = flags & FORMAT_MESSAGE_MAX_WIDTH_MASK;
	u32 err = 0, ret = 0;

	if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
	{
		if (!buffer) { set_error(state, ERROR_NOT_ENOUGH_MEMORY); return 0; }
		put32(buffer, 0);
	}
	if (size >= 32768u) { set_error(state, ERROR_INVALID_PARAMETER); return 0; }
	if (width == 0xffu) width = ~0u;

	const GPTR src = get_message(flags, source, msgid, langid, 1, &err);
	if (!src) { set_error(state, err); return 0; }

	Out o = { heapAlloc(65536u), 0, 32768u };
	if (!o.buf) { heapFree(src); set_error(state, ERROR_NOT_ENOUGH_MEMORY); return 0; }
	const int isArray = (flags & FORMAT_MESSAGE_ARGUMENT_ARRAY) != 0u;
	GPTR after = 0;
	const u32 status = format_message(src, width, (flags & FORMAT_MESSAGE_IGNORE_INSERTS) != 0u, 1, isArray,
	                                  isArray ? args : list_of(flags, args), args != 0u, &o, &after);
	heapFree(src);

	if (status == ST_OVERFLOW) set_error(state, ERROR_INSUFFICIENT_BUFFER);
	else if (status) set_error(state, status);
	else
	{
		if (args && !isArray) { put32(args, after); }
		const u32 len = o.pos;		/* code page 1252 is one byte a character, the NUL included */
		if (len <= 1u) set_error(state, ERROR_NO_WORK_DONE);
		else
		{
			GPTR out = buffer;
			u32 cap = size;
			if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
			{
				cap = size > len ? size : len;
				out = heapAlloc(cap);
				if (!out) { set_error(state, ERROR_NOT_ENOUGH_MEMORY); heapFree(o.buf); return 0; }
				zero_guest(out, cap);
				put32(buffer, out);
			}
			if (out != buffer || len <= size)
			{
				for (u32 i = 0; i < len; i++)
				{
					const i32 b = codePageEncode(1252u, rd16(o.buf + 2u * i), 0);
					put8(out + i, b < 0 ? '?' : (u32)b);
				}
				guestWritten(out, len);
				ret = len - 1u;
			}
			else set_error(state, ERROR_INSUFFICIENT_BUFFER);
		}
	}
	heapFree(o.buf);
	return ret;
}

EXPORT("FormatMessageW") u32 FormatMessageW(u32 flags, GPTR source, u32 msgid, u32 langid, GPTR buffer, u32 size, GPTR args, u32 state)
{
	u32 width = flags & FORMAT_MESSAGE_MAX_WIDTH_MASK;
	u32 err = 0, status;

	if (!buffer) { set_error(state, ERROR_INVALID_PARAMETER); return 0; }
	if (width == 0xffu) width = ~0u;
	if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) put32(buffer, 0);

	const GPTR src = get_message(flags, source, msgid, langid, 0, &err);
	if (!src) { set_error(state, err); return 0; }
	const int ignore = (flags & FORMAT_MESSAGE_IGNORE_INSERTS) != 0u;
	const int isArray = (flags & FORMAT_MESSAGE_ARGUMENT_ARRAY) != 0u;
	u32 retsize = 0;

	if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
	{
		/* The va_list is copied here, as Wine's va_copy: the caller's is not advanced. */
		u32 alloc = size * 2u > 65536u ? size * 2u : 65536u;
		for (;;)
		{
			Out o = { heapAlloc(alloc), 0, alloc / 2u };
			if (!o.buf) { status = ERROR_NOT_ENOUGH_MEMORY; break; }
			status = format_message(src, width, ignore, 0, isArray, isArray ? args : list_of(flags, args),
			                        args != 0u, &o, 0);
			if (!status)
			{
				retsize = o.pos;
				if (retsize <= 1u) heapFree(o.buf);
				else
				{
					const u32 keep = retsize > size ? retsize : size;
					const GPTR block = heapAlloc(2u * keep);
					if (!block) { heapFree(o.buf); status = ERROR_NOT_ENOUGH_MEMORY; break; }
					zero_guest(block, 2u * keep);
					__builtin_memcpy(guest(block, 2u * retsize), guest(o.buf, 2u * retsize), 2u * retsize);
					guestWritten(block, 2u * retsize);
					heapFree(o.buf);
					put32(buffer, block);
				}
				break;
			}
			heapFree(o.buf);
			if (status != ST_OVERFLOW) break;
			alloc *= 2u;
		}
	}
	else
	{
		Out o = { buffer, 0, size };
		GPTR after = 0;
		status = format_message(src, width, ignore, 0, isArray, isArray ? args : list_of(flags, args), args != 0u, &o, &after);
		if (o.pos) guestWritten(buffer, 2u * o.pos);
		if (!status) { retsize = o.pos; if (args && !isArray) put32(args, after); }
	}
	heapFree(src);

	if (status == ST_OVERFLOW)
	{
		if (size) put16(buffer + 2u * (size - 1u), 0);
		set_error(state, ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	if (status) { set_error(state, status); return 0; }
	if (retsize <= 1u) set_error(state, ERROR_NO_WORK_DONE);
	return retsize ? retsize - 1u : 0u;
}
