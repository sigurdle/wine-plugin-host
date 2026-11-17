/*
 * The EDIT control's editing: typing (WM_CHAR), the caret and Delete / Insert keys (WM_KEYDOWN),
 * EM_REPLACESEL with its one-step undo buffer, EM_UNDO / WM_UNDO, the clipboard (WM_COPY, WM_CUT,
 * WM_PASTE, WM_CLEAR), and the mouse placing the caret and dragging a selection.
 *
 * Ported from Wine dlls/user32/edit.c (`EDIT_WM_Char`, `EDIT_WM_KeyDown`, `EDIT_EM_ReplaceSel`,
 * `EDIT_EM_Undo`, `EDIT_EM_CanUndo`, `EDIT_EM_EmptyUndoBuffer`, `EDIT_WM_Paste`, `EDIT_WM_Copy`,
 * `EDIT_WM_Clear`, `EDIT_WM_Cut`, `EDIT_WM_LButtonDown`, `EDIT_WM_MouseMove`, `EDIT_WM_LButtonUp`,
 * `EDIT_CharFromPos`):
 *
 *	Copyright  David W. Metcalfe, 1994
 *	Copyright  William Magro, 1995, 1996
 *	Copyright  Frans van Dorsselaer, 1996, 1997
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
 * Over the EDIT it does not own: `e` is controls.js `EditControl` -- `text`, `setUserText(s)`,
 * `selStart` / `selEnd` (unordered) and `selection` (ordered, clamped), `textLimit`, `style`,
 * `ctlId`, `multiline`, `readOnly`, and the undo buffer `undoText` / `undoPos` / `undoInsert`
 * (Wine's undo_text, undo_position, undo_insert_count). `ctx` is its class context (Dialog.js
 * `#classCtx`): send, post, dlgItem, focus, keyDown, queue (the capture), hwnd, parent, cpu, textWidth,
 * clientWidth.
 */
import { WM_COMMAND, WM_CLOSE, WM_KEYDOWN, EN_CHANGE, EN_UPDATE, MK_SHIFT, VK_SHIFT, VK_CONTROL, VK_MENU } from './messages.js';
import { VK_LEFT, VK_RIGHT, VK_HOME, VK_END, VK_DELETE, VK_INSERT, VK_TAB, VK_RETURN, VK_ESCAPE } from './keyboard.js';
import { CF_UNICODETEXT, openClipboard, closeClipboard, emptyClipboard, clipboardText, setClipboardData,
         getClipboardData } from './clipboard.js';

export const WM_CUT = 0x0300, WM_COPY = 0x0301, WM_PASTE = 0x0302, WM_CLEAR = 0x0303, WM_UNDO = 0x0304;
export const EM_SETSEL = 0x00b1, EM_REPLACESEL = 0x00c2, EM_CANUNDO = 0x00c6, EM_UNDO = 0x00c7, EM_EMPTYUNDOBUFFER = 0x00cd;
const EN_MAXTEXT = 0x0501;
const ES_CENTER = 0x0001, ES_RIGHT = 0x0002, ES_MULTILINE = 0x0004, ES_PASSWORD = 0x0020, ES_WANTRETURN = 0x1000;
const WM_NEXTDLGCTL = 0x0028, DM_GETDEFID = 0x0400, DC_HASDEFID = 0x534b;

const notify = (e, ctx, code) => ctx.send(ctx.parent, WM_COMMAND, ((code << 16) | (e.ctlId & 0xffff)) >>> 0, ctx.hwnd);

/** EDIT_EM_EmptyUndoBuffer. */
export function emptyUndo(e)
{
	e.undoInsert = 0;
	e.undoText = '';
}

/** EDIT_EM_CanUndo. */
export const canUndo = e => e.undoInsert !== 0 || e.undoText.length !== 0;

/**
 * EDIT_EM_ReplaceSel: the selection becomes `str`, cut to the text limit (EN_MAXTEXT when that
 * refused any of it), the caret after it; the undo buffer records the deletion and the insertion
 * (`undo`), or is emptied; the parent hears EN_UPDATE then EN_CHANGE when `update`.
 */
export function replaceSel(e, str, ctx, undo = true, update = true)
{
	const text = String(e.text ?? ''), [s, end] = e.selection;
	if (s === end && !str) return;
	const room = Math.max(0, e.textLimit - (text.length - (end - s)));
	if (str.length > room)
	{
		notify(e, ctx, EN_MAXTEXT);
		str = str.slice(0, room);
		if (!str && s === end) return;
	}
	if (end !== s)
	{
		if (undo)
		{
			const gone = text.slice(s, end);
			if (!e.undoInsert && e.undoText && s === e.undoPos) e.undoText += gone;		// extended to the right
			else if (!e.undoInsert && e.undoText && end === e.undoPos) { e.undoText = gone + e.undoText; e.undoPos = s; }
			else { e.undoText = gone; e.undoPos = s; }
			// any deletion makes the old insertion-undo invalid
			e.undoInsert = 0;
		}
		else emptyUndo(e);
	}
	if (str)
	{
		if (undo)
		{
			if (s === e.undoPos || (e.undoInsert && s === e.undoPos + e.undoInsert)) e.undoInsert += str.length;
			else { e.undoPos = s; e.undoInsert = str.length; e.undoText = ''; }
		}
		else emptyUndo(e);
	}
	e.setUserText(text.slice(0, s) + str + text.slice(end));
	e.selStart = e.selEnd = s + str.length;
	if (update) { notify(e, ctx, EN_UPDATE); notify(e, ctx, EN_CHANGE); }
}

/**
 * EDIT_EM_Undo: what the last run of typing replaced comes back, selected; the undo buffer then
 * holds the typing, so a second undo redoes it. A read-only single-line EDIT returns TRUE and does
 * nothing.
 */
export function undo(e, ctx)
{
	if (e.readOnly) return e.multiline ? 0 : 1;
	const utext = e.undoText;
	e.selStart = e.undoPos; e.selEnd = e.undoPos + e.undoInsert;
	emptyUndo(e);
	replaceSel(e, utext, ctx, true, true);
	e.selStart = e.undoPos; e.selEnd = e.undoPos + e.undoInsert;
	// send the notification after the selection start and end are set
	notify(e, ctx, EN_CHANGE);
	return 1;
}

/** EDIT_WM_Copy: the selection, as CF_UNICODETEXT. Nothing selected copies nothing. */
export function copy(e, ctx)
{
	const [s, end] = e.selection;
	if (s === end) return;
	const cpu = ctx.cpu, str = String(e.text ?? '').slice(s, end);
	const h = cpu.mem.alloc((str.length + 1) * 2);
	for (let i = 0; i < str.length; i++) cpu.mem.dv.setUint16(h + i * 2, str.charCodeAt(i), true);
	cpu.mem.dv.setUint16(h + str.length * 2, 0, true);
	openClipboard(cpu, ctx.hwnd);
	emptyClipboard(cpu);
	setClipboardData(cpu, CF_UNICODETEXT, h);
	closeClipboard(cpu);
}

/** EDIT_WM_Clear: the selection goes. */
export function clear(e, ctx)
{
	if (e.readOnly) return;
	replaceSel(e, '', ctx, true, true);
}

/** EDIT_WM_Cut. */
export function cut(e, ctx)
{
	copy(e, ctx);
	clear(e, ctx);
}

/**
 * EDIT_WM_Paste: CF_UNICODETEXT replaces the selection -- for a single-line EDIT only up to the
 * first line break. NOT filtered by ES_NUMBER: Windows documents that a paste of non-digits gets
 * into an ES_NUMBER box, and Wine's paste does not look. ES_PASSWORD clears the selection even
 * when the clipboard holds no text.
 */
export function paste(e, ctx)
{
	if (e.readOnly) return;
	const cpu = ctx.cpu;
	openClipboard(cpu, ctx.hwnd);
	const h = getClipboardData(cpu, CF_UNICODETEXT);
	if (h)
	{
		let src = clipboardTextAt(cpu, h);
		if (!(e.style & ES_MULTILINE))
		{
			const nl = src.indexOf('\n');
			if (nl >= 0) src = src.slice(0, nl > 0 && src[nl - 1] === '\r' ? nl - 1 : nl);
		}
		replaceSel(e, src, ctx, true, true);
	}
	else if (e.style & ES_PASSWORD) replaceSel(e, '', ctx, true, true);
	closeClipboard(cpu);
}

/** The UTF-16 text a clipboard handle holds. */
function clipboardTextAt(cpu, h)
{
	const dv = cpu.mem.dv;
	let s = '';
	for (let at = h >>> 0; ; at += 2) { const c = dv.getUint16(at, true); if (!c) break; s += String.fromCharCode(c); }
	return s;
}

/**
 * EDIT_WM_Char. Backspace deletes the selection or the character before the caret (not with
 * Control held); a printable character replaces the selection; ES_NUMBER refuses anything but a
 * digit (Windows beeps -- MessageBeep is silent here); Tab and Enter do nothing in a single-line
 * EDIT (a dialog has taken them already). ^C, ^V, ^X and ^Z are the EDIT sending itself WM_COPY,
 * WM_PASTE, WM_CUT and WM_UNDO, so a subclass sees them. ^A selects everything: Windows 11's EDIT
 * (comctl32 6, which Photoshop's manifest gives every plugin dialog) does; Wine's user32 EDIT does
 * not.
 */
export function onChar(e, c, ctx)
{
	const control = ctx.keyDown(VK_CONTROL), password = (e.style & ES_PASSWORD) !== 0;
	switch (c)
	{
		case 0x0d:
			// A multiline EDIT in a dialog takes Enter only with ES_WANTRETURN (the dialog had it).
			if (e.multiline && !(e.style & ES_WANTRETURN) && e.dialogMode) return;
			// falls through
		case 0x0a:
			if (!e.multiline) return;
			if (e.readOnly) homeThenDown(e);
			else replaceSel(e, '\r\n', ctx, true, true);
			return;
		case 0x09:
			if (e.multiline && !e.readOnly && !e.dialogMode) replaceSel(e, '\t', ctx, true, true);
			return;
		case 0x08:
			if (e.readOnly || control) return;
			if (e.selStart === e.selEnd)
			{
				const [s] = e.selection;
				if (s === 0) return;
				e.selStart = s - 1; e.selEnd = s;
			}
			clear(e, ctx);
			return;
		case 0x01: e.selStart = 0; e.selEnd = String(e.text ?? '').length; return;
		case 0x03: if (!password) ctx.send(ctx.hwnd, WM_COPY, 0, 0); return;
		case 0x16: if (!e.readOnly) ctx.send(ctx.hwnd, WM_PASTE, 0, 0); return;
		case 0x18: if (!(e.readOnly || password)) ctx.send(ctx.hwnd, WM_CUT, 0, 0); return;
		case 0x1a: if (!e.readOnly) ctx.send(ctx.hwnd, WM_UNDO, 0, 0); return;
		default:
			if ((e.style & 0x2000) && !(c >= 0x30 && c <= 0x39)) return;		// ES_NUMBER
			if (!e.readOnly && c >= 0x20 && c !== 0x7f) replaceSel(e, String.fromCharCode(c), ctx, true, true);
	}
}

/**
 * Enter in a read-only multiline EDIT: EDIT_MoveHome then EDIT_MoveDown_ML, neither extending -- the
 * caret to the start of the next line, or of its own on the last (MoveDown_ML's EDIT_CharFromPos
 * below the last line stays on it). Lines are the text's hard breaks; this EDIT does not wrap.
 * [REF wine dlls/user32/edit.c EDIT_WM_Char "case '\n': if (es->style & ES_MULTILINE) { if
 * (es->style & ES_READONLY) { EDIT_MoveHome(es, FALSE, FALSE); EDIT_MoveDown_ML(es, FALSE); }"]
 */
function homeThenDown(e)
{
	const text = String(e.text ?? ''), caret = Math.min(e.selEnd, text.length);
	const next = text.indexOf('\n', caret);
	e.selStart = e.selEnd = next < 0 ? text.lastIndexOf('\n', caret - 1) + 1 : next + 1;
}

/**
 * EDIT_WM_KeyDown, single line: Left and Right by a character, Home and End to the ends, Shift
 * extending the selection; Delete removes the selection (Shift+Delete CUTS it) or the character
 * after the caret (Shift: before it; Control: to the end); Shift+Insert pastes and Ctrl+Insert
 * copies. Nothing with Alt held.
 */
export function onKeyDown(e, vk, ctx)
{
	if (ctx.keyDown(VK_MENU)) return;
	const len = String(e.text ?? '').length, shift = ctx.keyDown(VK_SHIFT), control = ctx.keyDown(VK_CONTROL);
	const caret = Math.min(e.selEnd, len);
	const move = (to) => { e.selEnd = Math.max(0, Math.min(to, len)); if (!shift) e.selStart = e.selEnd; };
	switch (vk)
	{
		case VK_LEFT: { const [s, end] = e.selection; if (!shift && s !== end) move(s); else move(caret - 1); break; }
		case VK_RIGHT: { const [s, end] = e.selection; if (!shift && s !== end) move(end); else move(caret + 1); break; }
		case VK_HOME: move(0); break;
		case VK_END: move(len); break;
		case VK_DELETE: {
			if (e.readOnly || (shift && control)) break;
			const [s, end] = e.selection;
			if (s !== end) { if (shift) cut(e, ctx); else clear(e, ctx); break; }
			e.selStart = e.selEnd = s;
			if (shift) { if (s === 0) break; e.selStart = s - 1; }
			else if (control) e.selEnd = len;
			else { if (s >= len) break; e.selEnd = s + 1; }
			clear(e, ctx);
			break;
		}
		case VK_INSERT:
			if (shift) { if (!e.readOnly) paste(e, ctx); }
			else if (control) copy(e, ctx);
			break;
		// IN A DIALOG (`dialogMode`) a multiline EDIT, which asked for every key (DLGC_WANTALLKEYS),
		// does the dialog's keys itself: Enter without ES_WANTRETURN focuses the default button and
		// posts it the key, Esc posts the dialog WM_CLOSE, Tab moves the dialog's focus.
		case VK_RETURN: {
			if (e.multiline && (e.style & ES_WANTRETURN)) break;
			if (!e.dialogMode || control) break;
			const dw = ctx.send(ctx.parent, DM_GETDEFID, 0, 0) >>> 0;
			if ((dw >>> 16) !== DC_HASDEFID) break;
			const def = ctx.dlgItem(ctx.parent, dw & 0xffff);
			if (!def) break;
			ctx.send(ctx.parent, WM_NEXTDLGCTL, def, 1);
			ctx.post(def, WM_KEYDOWN, VK_RETURN, 0);
			break;
		}
		case VK_ESCAPE:
			if (e.multiline && e.dialogMode) ctx.post(ctx.parent, WM_CLOSE, 0, 0);
			break;
		case VK_TAB:
			if (e.multiline && e.dialogMode) ctx.send(ctx.parent, WM_NEXTDLGCTL, shift ? 1 : 0, 0);
			break;
	}
}

/**
 * EDIT_CharFromPos, one line: the character boundary nearest `x` (client pixels) in the text as
 * drawn -- in the control's font, from its left margin, or centered / right-aligned by its style;
 * an ES_PASSWORD box measures its asterisks.
 */
export function charFromPos(e, x, ctx)
{
	const raw = String(e.text ?? ''), shown = (e.style & ES_PASSWORD) ? '*'.repeat(raw.length) : raw;
	const full = ctx.textWidth(shown), room = ctx.clientWidth();
	const left = (e.style & ES_RIGHT) ? room - EDIT_MARGIN - full
		: (e.style & ES_CENTER) ? ((room - full) >> 1) : EDIT_MARGIN;
	let prev = 0;
	for (let i = 1; i <= shown.length; i++)
	{
		const w = ctx.textWidth(shown.slice(0, i));
		if (x - left < (prev + w) / 2) return i - 1;
		prev = w;
	}
	return shown.length;
}

/**
 * The left and right margin of the text in the EDIT's client area. Windows sets it from the font
 * (EC_USEFONTINFO): a few pixels for the dialog fonts plugins use. The app's box draws at the same
 * inset (PSFilterDialogUI's edit box).
 */
export const EDIT_MARGIN = 2;

/** EDIT_WM_LButtonDown: the capture, the caret at the clicked character (Shift extends), the focus. */
export function lButtonDown(e, keys, x, ctx)
{
	if (ctx.queue) ctx.queue.capture = ctx.hwnd;
	e.capturing = true;
	const at = charFromPos(e, x, ctx);
	e.selStart = (keys & MK_SHIFT) ? e.selStart : at;
	e.selEnd = at;
	if (!ctx.focused()) ctx.focus();
}

/** EDIT_WM_MouseMove: while it holds the capture, the selection's end follows the pointer. */
export function mouseMove(e, x, ctx)
{
	if (!e.capturing || !ctx.queue || ctx.queue.capture !== ctx.hwnd) return;
	e.selEnd = charFromPos(e, x, ctx);
}

/** EDIT_WM_LButtonUp. */
export function lButtonUp(e, ctx)
{
	if (e.capturing && ctx.queue && ctx.queue.capture === ctx.hwnd) ctx.queue.capture = 0;
	e.capturing = false;
}
