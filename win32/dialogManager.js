/*
 * The dialog manager's keyboard: IsDialogMessage, GetNextDlgTabItem, GetNextDlgGroupItem, the
 * mnemonic search, and DefDlgProc's WM_NEXTDLGCTL / DM_GETDEFID / DM_SETDEFID with the default push
 * button's restyling.
 *
 * Ported from Wine dlls/user32/dialog.c (`IsDialogMessageW`, `DIALOG_IsAccelerator`,
 * `DIALOG_FindMsgDestination`, `GetNextDlgGroupItem`, `DIALOG_GetNextTabItem`, `GetNextDlgTabItem`,
 * `DIALOG_FixChildrenOnChangeFocus`) and dlls/user32/defdlg.c (`DEFDLG_SetFocus`, `DEFDLG_SaveFocus`,
 * `DEFDLG_RestoreFocus`, `DEFDLG_FindDefButton`, `DEFDLG_SetDefId`, `DEFDLG_SetDefButton`,
 * WM_NEXTDLGCTL, DM_GETDEFID, DM_SETDEFID):
 *
 * Copyright 1993, 1994, 1996 Alexandre Julliard
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
 * Over a window tree it does not own: `ops` is the host's window manager (Dialog.js `#dialogOps`).
 *
 *   isWindow(h) isChild(parent, h) parent(h)        the tree
 *   child(h) next(h) prev(h) first(h) last(h)       GetWindow GW_CHILD / HWNDNEXT / HWNDPREV / HWNDFIRST / HWNDLAST
 *   style(h) exStyle(h) text(h) ctlId(h)            GetWindowLong, GetWindowText, GetDlgCtrlID
 *   dlgItem(dlg, id) isDialog(h) isEnabled(h)       GetDlgItem, NtUserGetDialogInfo, IsWindowEnabled
 *   send(h, msg, wParam, lParam)                    SendMessage
 *   getFocus() setFocus(h) shiftDown()              GetFocus, SetFocus, GetKeyState(VK_SHIFT) & 0x8000
 *   callMsgFilter(m) translate(m) dispatch(m)       CallMsgFilter(MSGF_DIALOGBOX), TranslateMessage, DispatchMessage
 *
 * A message `m` is { hwnd, msg, wParam, lParam, ptr } -- `ptr` the guest MSG, which WM_GETDLGCODE
 * is handed in lParam.
 */
import { WM_KEYDOWN, WM_COMMAND, BN_CLICKED } from "./messages.js";
import { WM_CHAR, WM_SYSCHAR, VK_TAB, VK_RIGHT, VK_DOWN, VK_LEFT, VK_UP, VK_CANCEL, VK_ESCAPE,
         VK_EXECUTE, VK_RETURN } from "./keyboard.js";

const WS_VISIBLE = 0x10000000, WS_DISABLED = 0x08000000, WS_GROUP = 0x00020000, WS_TABSTOP = 0x00010000;
const WS_EX_CONTROLPARENT = 0x00010000, DS_CONTROL = 0x0400;
export const WM_NEXTDLGCTL = 0x0028, DM_GETDEFID = 0x0400, DM_SETDEFID = 0x0401, DC_HASDEFID = 0x534b;
const WM_GETDLGCODE = 0x0087, WM_GETTEXTLENGTH = 0x000e, EM_SETSEL = 0x00b1;
const BM_GETCHECK = 0x00f0, BM_SETSTYLE = 0x00f4, BM_CLICK = 0x00f5, BS_TYPEMASK = 0x0f, BS_PUSHBUTTON = 0,
      BS_DEFPUSHBUTTON = 1, BS_GROUPBOX = 7, BS_AUTORADIOBUTTON = 9;
const DLGC_WANTARROWS = 0x0001, DLGC_WANTTAB = 0x0002, DLGC_WANTMESSAGE = 0x0004, DLGC_HASSETSEL = 0x0008,
      DLGC_DEFPUSHBUTTON = 0x0010, DLGC_UNDEFPUSHBUTTON = 0x0020, DLGC_RADIOBUTTON = 0x0040, DLGC_WANTCHARS = 0x0080, DLGC_STATIC = 0x0100,
      DLGC_BUTTON = 0x2000;
const IDOK = 1, IDCANCEL = 2;

const shownEnabled = (ops, h) => (ops.style(h) & (WS_VISIBLE | WS_DISABLED)) === WS_VISIBLE;

/** dialog.c DIALOG_FindMsgDestination: up through DS_CONTROL children to the dialog that is not one. */
function findMsgDestination(ops, dlg)
{
	while (ops.style(dlg) & DS_CONTROL)
	{
		const p = ops.parent(dlg);
		if (!p || !ops.isDialog(p)) break;
		dlg = p;
	}
	return dlg;
}

/** dialog.c GetNextDlgGroupItem. */
export function getNextDlgGroupItem(ops, dlg, ctrl, previous)
{
	if (dlg === ctrl) ctrl = 0;
	if (!ctrl && previous) return 0;
	if (ctrl)
	{
		if (!ops.isChild(dlg, ctrl)) return 0;
	}
	else
	{
		if (!(ctrl = ops.child(dlg))) return 0;
		if (shownEnabled(ops, ctrl)) return ctrl;
	}
	let retvalue = ctrl, hwnd = ctrl, lastGroup = 0, looped = false, skipping = false;
	for (let guard = 0; guard < 10000; guard++)
	{
		let next = ops.next(hwnd);
		while (!next)
		{
			if (dlg === ops.parent(hwnd))
			{
				if (looped) return retvalue;
				looped = true;
				next = ops.child(dlg);
			}
			else
			{
				hwnd = ops.parent(hwnd);
				next = ops.next(hwnd);
			}
		}
		hwnd = next;
		while ((ops.exStyle(hwnd) & WS_EX_CONTROLPARENT) && shownEnabled(ops, hwnd) && (next = ops.child(hwnd))) hwnd = next;
		if (ops.style(hwnd) & WS_GROUP)
		{
			lastGroup = hwnd;
			if (!skipping) skipping = true;
		}
		if (hwnd === ctrl)
		{
			if (!skipping) break;
			if (lastGroup === hwnd) break;
			hwnd = lastGroup;
			skipping = false;
			looped = false;
		}
		if (!skipping && shownEnabled(ops, hwnd))
		{
			retvalue = hwnd;
			if (!previous) break;
		}
	}
	return retvalue;
}

/** dialog.c DIALOG_GetNextTabItem. */
function nextTabItem(ops, main, dlg, ctrl, previous)
{
	const step = h => previous ? ops.prev(h) : ops.next(h);
	let first = 0, ret = 0;
	if (!ctrl)
	{
		first = ops.child(dlg);
		if (previous && first) first = ops.last(first);
	}
	else if (ops.isChild(main, ctrl))
	{
		first = step(ctrl);
		if (!first)
		{
			if (ops.parent(ctrl) !== main) first = step(ops.parent(ctrl));
			else first = previous ? ops.last(ctrl) : ops.first(ctrl);
		}
	}
	for (let guard = 0; first && guard < 10000; guard++)
	{
		const style = ops.style(first), ex = ops.exStyle(first);
		if ((ex & WS_EX_CONTROLPARENT) && (style & WS_VISIBLE) && !(style & WS_DISABLED))
		{
			const r = nextTabItem(ops, main, first, 0, previous);
			if (r) return r;
		}
		else if ((style & WS_TABSTOP) && (style & WS_VISIBLE) && !(style & WS_DISABLED)) return first;
		first = step(first);
	}
	if (ctrl)
	{
		for (let p = ops.parent(ctrl); p; p = ops.parent(p))
		{
			if (p === main) break;
			ret = nextTabItem(ops, main, ops.parent(p), p, previous);
			if (ret) break;
		}
		if (!ret) ret = nextTabItem(ops, main, main, 0, previous);
	}
	return ret || ctrl;
}

/** dialog.c GetNextDlgTabItem. */
export function getNextDlgTabItem(ops, dlg, ctrl, previous)
{
	if (dlg === ctrl) ctrl = 0;
	if (!ctrl && previous) return 0;
	return nextTabItem(ops, dlg, dlg, ctrl, previous);
}

/** defdlg.c DEFDLG_SetFocus: a control that keeps a selection has all of it selected as it gets the focus. */
function defDlgSetFocus(ops, ctrl)
{
	if (ops.send(ctrl, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL) ops.send(ctrl, EM_SETSEL, 0, -1);
	ops.setFocus(ctrl);
}

/**
 * defdlg.c DEFDLG_SaveFocus: the dialog keeps the control that has the focus (`saved`, by dialog --
 * DIALOGINFO.hwndFocus), at WM_ACTIVATE(WA_INACTIVE) and WM_SHOWWINDOW(FALSE).
 */
export function saveDlgFocus(ops, dlg, saved)
{
	const focus = ops.getFocus();
	if (!focus || !ops.isChild(dlg, focus)) return;
	saved.set(dlg, focus);
}

/**
 * defdlg.c DEFDLG_RestoreFocus: the kept control gets the focus back -- at WM_ACTIVATE(active)
 * (`justActivate`: only a kept one, by SetFocus) and at WM_SETFOCUS (else the first tab stop, else
 * the first shown enabled control, by DEFDLG_SetFocus). Nothing once EndDialog was called (`ended`).
 */
export function restoreDlgFocus(ops, dlg, saved, justActivate, ended)
{
	if (ended) return;
	let focus = saved.get(dlg) ?? 0;
	if (!ops.isWindow(focus) || focus === dlg)
	{
		if (justActivate) return;
		focus = getNextDlgTabItem(ops, dlg, 0, false);
		if (!focus) focus = getNextDlgGroupItem(ops, dlg, 0, false);
		if (!ops.isWindow(focus)) return;
	}
	if (justActivate) ops.setFocus(focus);
	else defDlgSetFocus(ops, focus);
	saved.delete(dlg);
}

/**
 * defdlg.c WM_NEXTDLGCTL: lParam nonzero -- wParam IS the control; else the next (wParam 0) or
 * previous tab stop from the focus. Then DEFDLG_SetDefButton: a push button that gets the focus is
 * the default one, and the dialog's own (`idResult`, its DM_SETDEFID id) is again when the focus
 * leaves the push buttons.
 */
export function nextDlgCtl(ops, dlg, wParam, lParam, idResult)
{
	const dest = lParam ? wParam >>> 0 : getNextDlgTabItem(ops, dlg, ops.getFocus(), wParam !== 0);
	if (dest) defDlgSetFocus(ops, dest);
	setDefButton(ops, dlg, idResult, dest);
	return 0;
}

/**
 * defdlg.c DEFDLG_SetDefButton: `hwndNew` becomes the default push button -- or, when it is no push
 * button, the dialog's default id's button does again -- and the old one a plain push button.
 */
function setDefButton(ops, dlg, idResult, hwndNew)
{
	let oldBtn = ops.dlgItem(dlg, idResult);
	let code = 0;
	if (hwndNew && !((code = ops.send(hwndNew, WM_GETDLGCODE, 0, 0)) & (DLGC_UNDEFPUSHBUTTON | DLGC_DEFPUSHBUTTON)))
	{
		hwndNew = oldBtn;
		code = hwndNew ? ops.send(hwndNew, WM_GETDLGCODE, 0, 0) : 0;
	}
	if (!oldBtn || !(ops.send(oldBtn, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON)) oldBtn = findDefButton(ops, dlg);
	if (oldBtn && oldBtn !== hwndNew) ops.send(oldBtn, BM_SETSTYLE, BS_PUSHBUTTON, 1);
	if (hwndNew && (code & DLGC_UNDEFPUSHBUTTON)) ops.send(hwndNew, BM_SETSTYLE, BS_DEFPUSHBUTTON, 1);
}

/**
 * defdlg.c DEFDLG_SetDefId (DM_SETDEFID): the dialog's default id is now `id` (the caller keeps it,
 * whatever this does); its button, a push button, becomes the default one and the old default
 * (`oldId`'s, else whichever says it is) a plain one. FALSE when `id` names a window that is no button.
 */
export function setDefId(ops, dlg, oldId, id)
{
	const newBtn = ops.dlgItem(dlg, id);
	let code = 0;
	if (newBtn && !((code = ops.send(newBtn, WM_GETDLGCODE, 0, 0)) & (DLGC_UNDEFPUSHBUTTON | DLGC_BUTTON))) return false;
	let oldBtn = ops.dlgItem(dlg, oldId);
	if (!oldBtn || !(ops.send(oldBtn, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON)) oldBtn = findDefButton(ops, dlg);
	if (oldBtn && oldBtn !== newBtn) ops.send(oldBtn, BM_SETSTYLE, BS_PUSHBUTTON, 1);
	if (newBtn && (code & DLGC_UNDEFPUSHBUTTON)) ops.send(newBtn, BM_SETSTYLE, BS_DEFPUSHBUTTON, 1);
	return true;
}

/**
 * dialog.c DIALOG_FixChildrenOnChangeFocus -- IsDialogMessage's Tab in a window that is no dialog:
 * every default push button in it (EnumChildWindows: all descendants) becomes a plain one; then
 * `next`, when it was the default, hands that back to the dialog's DM_GETDEFID button, and a plain
 * push button getting the focus becomes the default.
 */
function fixChildrenOnChangeFocus(ops, dlg, next)
{
	const nextCode = ops.send(next, WM_GETDLGCODE, 0, 0);
	const each = h => {
		for (let c = ops.child(h); c; c = ops.next(c))
		{
			if (ops.send(c, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON) ops.send(c, BM_SETSTYLE, BS_PUSHBUTTON, 1);
			each(c);
		}
	};
	each(dlg);
	if (nextCode & DLGC_DEFPUSHBUTTON)
	{
		const defId = ops.send(dlg, DM_GETDEFID, 0, 0);
		if ((defId >>> 16) !== DC_HASDEFID) return;
		const def = ops.dlgItem(dlg, defId & 0xffff);
		if (!def) return;
		const defCode = ops.send(def, WM_GETDLGCODE, 0, 0);
		if ((defCode & DLGC_BUTTON) && (defCode & DLGC_UNDEFPUSHBUTTON)) ops.send(def, BM_SETSTYLE, BS_DEFPUSHBUTTON, 1);
	}
	else if ((nextCode & DLGC_BUTTON) && (nextCode & DLGC_UNDEFPUSHBUTTON)) ops.send(next, BM_SETSTYLE, BS_DEFPUSHBUTTON, 1);
}

/** defdlg.c DEFDLG_FindDefButton: the first child (through control parents) that says it is the default push button. */
function findDefButton(ops, dlg)
{
	for (let c = ops.child(dlg); c; c = ops.next(c))
	{
		if (ops.send(c, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON) return c;
		if ((ops.exStyle(c) & WS_EX_CONTROLPARENT) && shownEnabled(ops, c))
		{
			const t = findDefButton(ops, c);
			if (t) return t;
		}
	}
	return 0;
}

/** defdlg.c DM_GETDEFID: the id DM_SETDEFID set (`defId`, 0 for none), else the default push button's. */
export function getDefId(ops, dlg, defId)
{
	if (defId) return ((DC_HASDEFID << 16) | (defId & 0xffff)) >>> 0;
	const b = findDefButton(ops, dlg);
	return b ? ((DC_HASDEFID << 16) | (ops.ctlId(b) & 0xffff)) >>> 0 : 0;
}

/** dialog.c DIALOG_IsAccelerator: the control whose "&x" mnemonic is `ch`, from `hwnd` on, round the dialog once. */
function isAccelerator(ops, hwnd, dlg, ch)
{
	const want = String.fromCharCode(ch).toUpperCase();
	let ctl = hwnd;
	for (let guard = 0; guard < 10000; guard++)
	{
		let next = 0;
		const style = ops.style(ctl);
		if ((style & (WS_VISIBLE | WS_DISABLED)) === WS_VISIBLE)
		{
			const code = ops.send(ctl, WM_GETDLGCODE, 0, 0);
			const text = (code & (DLGC_BUTTON | DLGC_STATIC)) ? String(ops.text(ctl) ?? "") : "";
			let i = -2;
			do { i = text.indexOf("&", i + 2); } while (i >= 0 && text[i + 1] === "&");
			if (i >= 0 && i + 1 < text.length && text[i + 1].toUpperCase() === want)
			{
				if ((code & DLGC_STATIC) || (style & BS_TYPEMASK) === BS_GROUPBOX)
				{
					ops.send(dlg, WM_NEXTDLGCTL, ctl, 1);
					ops.send(dlg, WM_NEXTDLGCTL, 0, 0);
				}
				else if (code & DLGC_BUTTON) ops.send(ctl, BM_CLICK, 0, 0);
				return true;
			}
			next = ops.child(ctl);
		}
		if (!next) next = ops.next(ctl);
		while (!next && ctl)
		{
			ctl = ops.parent(ctl);
			if (ctl === dlg)
			{
				if (hwnd === dlg) { next = hwnd; break; }
				next = ops.child(dlg);
			}
			else next = ops.next(ctl);
		}
		ctl = next;
		if (!ctl || ctl === hwnd) break;
	}
	return false;
}

/**
 * dialog.c IsDialogMessageW. TRUE when the message was the dialog's -- processed here, translated
 * and dispatched included -- and FALSE when it is for no window of `dlg` (the caller translates
 * and dispatches it itself).
 */
export function isDialogMessage(ops, dlg, m)
{
	if (!ops.isWindow(dlg)) return false;
	if (ops.callMsgFilter(m)) return true;
	if (dlg !== m.hwnd && !ops.isChild(dlg, m.hwnd)) return false;
	dlg = findMsgDestination(ops, dlg);

	let code;
	switch (m.msg)
	{
		case WM_KEYDOWN:
			code = ops.send(m.hwnd, WM_GETDLGCODE, m.wParam, m.ptr);
			if (code & DLGC_WANTMESSAGE) break;
			switch (m.wParam)
			{
				case VK_TAB:
					if (code & DLGC_WANTTAB) break;
					if (ops.isDialog(dlg)) ops.send(dlg, WM_NEXTDLGCTL, ops.shiftDown() ? 1 : 0, 0);
					else
					{
						const focus = ops.getFocus();
						const next = getNextDlgTabItem(ops, dlg, focus === dlg ? 0 : focus, ops.shiftDown());
						if (!next) return false;
						if (ops.send(next, WM_GETDLGCODE, m.wParam, m.ptr) & DLGC_HASSETSEL)
							ops.send(next, EM_SETSEL, 0, ops.send(next, WM_GETTEXTLENGTH, 0, 0));
						ops.setFocus(next);
						fixChildrenOnChangeFocus(ops, dlg, next);
					}
					return true;
				case VK_RIGHT: case VK_DOWN: case VK_LEFT: case VK_UP: {
					if (code & DLGC_WANTARROWS) break;
					const previous = m.wParam === VK_LEFT || m.wParam === VK_UP;
					const next = getNextDlgGroupItem(ops, dlg, m.hwnd, previous);
					if (next && ops.send(next, WM_GETDLGCODE, m.wParam, m.ptr) === (DLGC_BUTTON | DLGC_RADIOBUTTON))
					{
						ops.setFocus(next);
						if ((ops.style(next) & BS_TYPEMASK) === BS_AUTORADIOBUTTON && ops.send(next, BM_GETCHECK, 0, 0) !== 1)
							ops.send(next, BM_CLICK, 1, 0);
					}
					else ops.send(dlg, WM_NEXTDLGCTL, next, 1);
					return true;
				}
				case VK_CANCEL: case VK_ESCAPE:
					ops.send(dlg, WM_COMMAND, IDCANCEL, ops.dlgItem(dlg, IDCANCEL));
					return true;
				case VK_EXECUTE: case VK_RETURN: {
					const focus = ops.getFocus();
					let dw;
					if (ops.isChild(dlg, focus) && (ops.send(focus, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON))
						ops.send(dlg, WM_COMMAND, ((BN_CLICKED << 16) | (ops.ctlId(focus) & 0xffff)) >>> 0, focus);
					else if (((dw = ops.send(dlg, DM_GETDEFID, 0, 0)) >>> 16) === DC_HASDEFID)
					{
						const def = ops.dlgItem(dlg, dw & 0xffff);
						if (!def || ops.isEnabled(def))
							ops.send(dlg, WM_COMMAND, ((BN_CLICKED << 16) | (dw & 0xffff)) >>> 0, def);
					}
					else ops.send(dlg, WM_COMMAND, IDOK, ops.dlgItem(dlg, IDOK));
					return true;
				}
			}
			break;
		case WM_CHAR:
			code = ops.send(m.hwnd, WM_GETDLGCODE, m.wParam, m.ptr);
			if (code & (DLGC_WANTCHARS | DLGC_WANTMESSAGE)) break;
			if (m.wParam === 0x09 && (code & DLGC_WANTTAB)) break;
			if (isAccelerator(ops, m.hwnd, dlg, m.wParam)) return true;
			break;
		case WM_SYSCHAR:
			if (isAccelerator(ops, m.hwnd, dlg, m.wParam)) return true;
			break;
	}
	ops.translate(m);
	ops.dispatch(m);
	return true;
}
