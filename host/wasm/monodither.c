/*
 * A solid brush on a monochrome surface: the 8x8 ordered-dither pattern it becomes.
 *
 * Derived from Wine's DIB driver, dlls/win32u/dibdrv/primitives.c (`bayer_8x8`,
 * `create_dither_masks_1`) and dlls/win32u/dibdrv/objects.c (`brush_needs_dithering`):
 *
 * Copyright 2011 Huw Davies
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
 * WHERE THIS DIFFERS FROM WINE, AND WHY: Windows 11 itself is the oracle
 * (tmp/win32-oracle/monodither-2026-11-15.log, monopen-2026-11-15.log). Wine's matrix is the one
 * Windows uses, but Wine lights a pixel when `gray + bayer > 63`, which puts 0x808080's first row
 * at 0x55; Windows answers 0xaa -- a pixel is WHITE when its matrix entry is BELOW the level. The
 * level is fitted to every oracle color, bit for bit: luminance 30/59/11 (R, G, B of the
 * COLORREF), then (Y + 1) >> 2, so 0x7f7f7f is 32 (half), 0xfefefe 63 (one black pixel) and
 * 0x020202 0 (all black). "color 0000ff: aaaa 4444 ..." is level 19, "color 00ff00: eeee ..." 37.
 * A color that IS black or white is not dithered (Wine's `brush_needs_dithering`).
 */

static const unsigned char bayer_8x8[8][8] =
{
	{  0, 32,  8, 40,  2, 34, 10, 42 },
	{ 48, 16, 56, 24, 50, 18, 58, 26 },
	{ 12, 44,  4, 36, 14, 46,  6, 38 },
	{ 60, 28, 52, 20, 62, 30, 54, 22 },
	{  3, 35, 11, 43,  1, 33,  9, 41 },
	{ 51, 19, 59, 27, 49, 17, 57, 25 },
	{ 15, 47,  7, 39, 13, 45,  5, 37 },
	{ 63, 31, 55, 23, 61, 29, 53, 21 }
};

/** Whether a solid brush of this COLORREF is a dither on a 1-bpp surface: anything but exact black or white. */
unsigned int monoDitherNeeded(unsigned int colorRef)
{
	colorRef &= 0xffffffu;
	return colorRef != 0u && colorRef != 0xffffffu;
}

/** One row of the 8x8 pattern, the high bit the leftmost pixel, a 1 white. `y` is taken mod 8. */
unsigned int monoDitherRow(unsigned int colorRef, int y)
{
	unsigned int r = colorRef & 0xffu, g = (colorRef >> 8) & 0xffu, b = (colorRef >> 16) & 0xffu;
	unsigned int level = ((30u * r + 59u * g + 11u * b) / 100u + 1u) >> 2;
	const unsigned char *row = bayer_8x8[y & 7];
	unsigned int bits = 0;
	for (int x = 0; x < 8; x++) if (row[x] < level) bits |= 0x80u >> x;
	return bits;
}

/** The pattern's pixel at (x, y) from the brush origin: 1 white, 0 black. */
unsigned int monoDitherBit(unsigned int colorRef, int x, int y)
{
	return (monoDitherRow(colorRef, y) >> (7 - (x & 7))) & 1u;
}
