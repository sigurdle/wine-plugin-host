/*
 * DrawEdge's rectangle edge: which system color each half of each side takes, and where it goes.
 *
 * Derived from Wine's dlls/user32/uitools.c (the LT/RB Inner/Outer tables and
 * `UITOOLS95_DrawRectEdge`):
 *
 * Copyright 1997 Dimitrie O. Paun
 * Copyright 1997 Bertho A. Stultiens
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
 * HELD TO WINDOWS 11 (nativeplugins/tests/fixtures/oracle-2332/monogdi.log, the "drawedge" lines:
 * a 16x16 1-bpp bitmap, where every tone is its own 8x8 dither and so tells the tones apart).
 * EDGE_RAISED's bottom row is COLOR_3DDKSHADOW ("aaaa") over an inner row of COLOR_3DSHADOW
 * ("d554"); BF_FLAT is a COLOR_3DSHADOW frame around a COLOR_3DFACE one; BF_MONO is a
 * COLOR_WINDOWFRAME frame around a COLOR_WINDOW one with a COLOR_WINDOW middle; BF_SOFT swaps the
 * two top-left tones, which is how a push button differs from a bare EDGE_RAISED.
 *
 * WHERE THIS DIFFERS FROM WINE: the return value and the middle are the caller's (gdi32.c
 * `edge_draw`), which fills the middle whatever the edge type.
 */

#define COLOR_WINDOW        5
#define COLOR_WINDOWFRAME   6
#define COLOR_BTNFACE       15
#define COLOR_BTNSHADOW     16
#define COLOR_BTNHIGHLIGHT  20
#define COLOR_3DDKSHADOW    21
#define COLOR_3DLIGHT       22

#define BDR_INNER_OUTER 0x000f
#define BF_LEFT    0x0001
#define BF_TOP     0x0002
#define BF_RIGHT   0x0004
#define BF_BOTTOM  0x0008
#define BF_TOPLEFT     (BF_TOP | BF_LEFT)
#define BF_TOPRIGHT    (BF_TOP | BF_RIGHT)
#define BF_BOTTOMLEFT  (BF_BOTTOM | BF_LEFT)
#define BF_BOTTOMRIGHT (BF_BOTTOM | BF_RIGHT)
#define BF_SOFT    0x1000
#define BF_FLAT    0x4000
#define BF_MONO    0x8000

static const signed char LTInnerNormal[] = {
	-1,           -1,                 -1,                 -1,
	-1,           COLOR_BTNHIGHLIGHT, COLOR_BTNHIGHLIGHT, -1,
	-1,           COLOR_3DDKSHADOW,   COLOR_3DDKSHADOW,   -1,
	-1,           -1,                 -1,                 -1
};

static const signed char LTOuterNormal[] = {
	-1,                 COLOR_3DLIGHT,     COLOR_BTNSHADOW, -1,
	COLOR_BTNHIGHLIGHT, COLOR_3DLIGHT,     COLOR_BTNSHADOW, -1,
	COLOR_3DDKSHADOW,   COLOR_3DLIGHT,     COLOR_BTNSHADOW, -1,
	-1,                 COLOR_3DLIGHT,     COLOR_BTNSHADOW, -1
};

static const signed char RBInnerNormal[] = {
	-1,           -1,                -1,              -1,
	-1,           COLOR_BTNSHADOW,   COLOR_BTNSHADOW, -1,
	-1,           COLOR_3DLIGHT,     COLOR_3DLIGHT,   -1,
	-1,           -1,                -1,              -1
};

static const signed char RBOuterNormal[] = {
	-1,              COLOR_3DDKSHADOW,  COLOR_BTNHIGHLIGHT, -1,
	COLOR_BTNSHADOW, COLOR_3DDKSHADOW,  COLOR_BTNHIGHLIGHT, -1,
	COLOR_3DLIGHT,   COLOR_3DDKSHADOW,  COLOR_BTNHIGHLIGHT, -1,
	-1,              COLOR_3DDKSHADOW,  COLOR_BTNHIGHLIGHT, -1
};

static const signed char LTInnerSoft[] = {
	-1,                  -1,                -1,              -1,
	-1,                  COLOR_3DLIGHT,     COLOR_3DLIGHT,   -1,
	-1,                  COLOR_BTNSHADOW,   COLOR_BTNSHADOW, -1,
	-1,                  -1,                -1,              -1
};

static const signed char LTOuterSoft[] = {
	-1,              COLOR_BTNHIGHLIGHT, COLOR_3DDKSHADOW, -1,
	COLOR_3DLIGHT,   COLOR_BTNHIGHLIGHT, COLOR_3DDKSHADOW, -1,
	COLOR_BTNSHADOW, COLOR_BTNHIGHLIGHT, COLOR_3DDKSHADOW, -1,
	-1,              COLOR_BTNHIGHLIGHT, COLOR_3DDKSHADOW, -1
};

#define RBInnerSoft RBInnerNormal   /* These are the same */
#define RBOuterSoft RBOuterNormal

static const signed char LTRBOuterMono[] = {
	-1,           COLOR_WINDOWFRAME, COLOR_WINDOWFRAME, COLOR_WINDOWFRAME,
	COLOR_WINDOW, COLOR_WINDOWFRAME, COLOR_WINDOWFRAME, COLOR_WINDOWFRAME,
	COLOR_WINDOW, COLOR_WINDOWFRAME, COLOR_WINDOWFRAME, COLOR_WINDOWFRAME,
	COLOR_WINDOW, COLOR_WINDOWFRAME, COLOR_WINDOWFRAME, COLOR_WINDOWFRAME,
};

static const signed char LTRBInnerMono[] = {
	-1, -1,           -1,           -1,
	-1, COLOR_WINDOW, COLOR_WINDOW, COLOR_WINDOW,
	-1, COLOR_WINDOW, COLOR_WINDOW, COLOR_WINDOW,
	-1, COLOR_WINDOW, COLOR_WINDOW, COLOR_WINDOW,
};

static const signed char LTRBOuterFlat[] = {
	-1,                COLOR_BTNSHADOW, COLOR_BTNSHADOW, COLOR_BTNSHADOW,
	COLOR_BTNFACE,     COLOR_BTNSHADOW, COLOR_BTNSHADOW, COLOR_BTNSHADOW,
	COLOR_BTNFACE,     COLOR_BTNSHADOW, COLOR_BTNSHADOW, COLOR_BTNSHADOW,
	COLOR_BTNFACE,     COLOR_BTNSHADOW, COLOR_BTNSHADOW, COLOR_BTNSHADOW,
};

static const signed char LTRBInnerFlat[] = {
	-1, -1,              -1,              -1,
	-1, COLOR_BTNFACE,     COLOR_BTNFACE,     COLOR_BTNFACE,
	-1, COLOR_BTNFACE,     COLOR_BTNFACE,     COLOR_BTNFACE,
	-1, COLOR_BTNFACE,     COLOR_BTNFACE,     COLOR_BTNFACE,
};

/** gdi32.c: one fill of a system color, logical, through the view `ctx` names; -1 draws nothing. */
void edgeFillSysColor(void *ctx, int l, int t, int r, int b, int sysColor);

/**
 * The outer frame, then the inner one, one pixel each, as fills in the order top, left, bottom,
 * right -- the later side wins a corner. `rc` (left, top, right, bottom) comes back deflated by
 * the frames each named side has, which is what BF_ADJUST writes back and what the middle fills.
 */
void wineDrawRectEdge(void *ctx, int *rc, unsigned int uType, unsigned int uFlags)
{
	signed char LTInnerI, LTOuterI;
	signed char RBInnerI, RBOuterI;
	const int width = 1;
	int left = rc[0], top = rc[1], right = rc[2], bottom = rc[3];
	int lbi_offset = 0, lti_offset = 0, rti_offset = 0, rbi_offset = 0;
	unsigned int idx = uType & BDR_INNER_OUTER;

	/* Determine the colors of the edges */
	if (uFlags & BF_MONO)
	{
		LTInnerI = RBInnerI = LTRBInnerMono[idx];
		LTOuterI = RBOuterI = LTRBOuterMono[idx];
	}
	else if (uFlags & BF_FLAT)
	{
		LTInnerI = RBInnerI = LTRBInnerFlat[idx];
		LTOuterI = RBOuterI = LTRBOuterFlat[idx];

		if (LTInnerI != -1) LTInnerI = RBInnerI = COLOR_BTNFACE;
	}
	else if (uFlags & BF_SOFT)
	{
		LTInnerI = LTInnerSoft[idx];
		LTOuterI = LTOuterSoft[idx];
		RBInnerI = RBInnerSoft[idx];
		RBOuterI = RBOuterSoft[idx];
	}
	else
	{
		LTInnerI = LTInnerNormal[idx];
		LTOuterI = LTOuterNormal[idx];
		RBInnerI = RBInnerNormal[idx];
		RBOuterI = RBOuterNormal[idx];
	}

	if ((uFlags & BF_BOTTOMLEFT) == BF_BOTTOMLEFT)   lbi_offset = width;
	if ((uFlags & BF_TOPRIGHT) == BF_TOPRIGHT)       rti_offset = width;
	if ((uFlags & BF_BOTTOMRIGHT) == BF_BOTTOMRIGHT) rbi_offset = width;
	if ((uFlags & BF_TOPLEFT) == BF_TOPLEFT)         lti_offset = width;

	/* Draw the outer edge */
	if (uFlags & BF_TOP)    edgeFillSysColor(ctx, left, top, right, top + width, LTOuterI);
	if (uFlags & BF_LEFT)   edgeFillSysColor(ctx, left, top, left + width, bottom, LTOuterI);
	if (uFlags & BF_BOTTOM) edgeFillSysColor(ctx, left, bottom - width, right, bottom, RBOuterI);
	if (uFlags & BF_RIGHT)  edgeFillSysColor(ctx, right - width, top, right, bottom, RBOuterI);

	/* Draw the inner edge */
	if (uFlags & BF_TOP)    edgeFillSysColor(ctx, left + lti_offset, top + width, right - rti_offset, top + 2 * width, LTInnerI);
	if (uFlags & BF_LEFT)   edgeFillSysColor(ctx, left + width, top + lti_offset, left + 2 * width, bottom - lbi_offset, LTInnerI);
	if (uFlags & BF_BOTTOM) edgeFillSysColor(ctx, left + lbi_offset, bottom - 2 * width, right - rbi_offset, bottom - width, RBInnerI);
	if (uFlags & BF_RIGHT)  edgeFillSysColor(ctx, right - 2 * width, top + rti_offset, right - width, bottom - rbi_offset, RBInnerI);

	{
		int add = (LTRBInnerMono[idx] != -1 ? width : 0) + (LTRBOuterMono[idx] != -1 ? width : 0);

		if (uFlags & BF_LEFT)   rc[0] += add;
		if (uFlags & BF_TOP)    rc[1] += add;
		if (uFlags & BF_RIGHT)  rc[2] -= add;
		if (uFlags & BF_BOTTOM) rc[3] -= add;
	}
}
