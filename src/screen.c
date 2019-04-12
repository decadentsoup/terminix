// screen.c - screen management routines
// Copyright (C) 2019 Megan Ruggiero. All rights reserved.
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <stdlib.h>
#include <string.h>
#include "terminix.h"

static const struct color default_palette[256] = {
	{0x00,0x00,0x00},{0x80,0x00,0x00},{0x00,0x80,0x00},{0x80,0x80,0x00},
	{0x00,0x00,0x80},{0x80,0x00,0x80},{0x00,0x80,0x80},{0xC0,0xC0,0xC0},
	{0x80,0x80,0x80},{0xFF,0x00,0x00},{0x00,0xFF,0x00},{0xFF,0xFF,0x00},
	{0x00,0x00,0xFF},{0xFF,0x00,0xFF},{0x00,0xFF,0xFF},{0xFF,0xFF,0xFF},
	{0x00,0x00,0x00},{0x00,0x00,0x5F},{0x00,0x00,0x87},{0x00,0x00,0xAF},
	{0x00,0x00,0xD7},{0x00,0x00,0xFF},{0x00,0x5F,0x00},{0x00,0x5F,0x5F},
	{0x00,0x5F,0x87},{0x00,0x5F,0xAF},{0x00,0x5F,0xD7},{0x00,0x5F,0xFF},
	{0x00,0x87,0x00},{0x00,0x87,0x5F},{0x00,0x87,0x87},{0x00,0x87,0xAF},
	{0x00,0x87,0xD7},{0x00,0x87,0xFF},{0x00,0xAF,0x00},{0x00,0xAF,0x5F},
	{0x00,0xAF,0x87},{0x00,0xAF,0xAF},{0x00,0xAF,0xD7},{0x00,0xAF,0xFF},
	{0x00,0xD7,0x00},{0x00,0xD7,0x5F},{0x00,0xD7,0x87},{0x00,0xD7,0xAF},
	{0x00,0xD7,0xD7},{0x00,0xD7,0xFF},{0x00,0xFF,0x00},{0x00,0xFF,0x5F},
	{0x00,0xFF,0x87},{0x00,0xFF,0xAF},{0x00,0xFF,0xD7},{0x00,0xFF,0xFF},
	{0x5F,0x00,0x00},{0x5F,0x00,0x5F},{0x5F,0x00,0x87},{0x5F,0x00,0xAF},
	{0x5F,0x00,0xD7},{0x5F,0x00,0xFF},{0x5F,0x5F,0x00},{0x5F,0x5F,0x5F},
	{0x5F,0x5F,0x87},{0x5F,0x5F,0xAF},{0x5F,0x5F,0xD7},{0x5F,0x5F,0xFF},
	{0x5F,0x87,0x00},{0x5F,0x87,0x5F},{0x5F,0x87,0x87},{0x5F,0x87,0xAF},
	{0x5F,0x87,0xD7},{0x5F,0x87,0xFF},{0x5F,0xAF,0x00},{0x5F,0xAF,0x5F},
	{0x5F,0xAF,0x87},{0x5F,0xAF,0xAF},{0x5F,0xAF,0xD7},{0x5F,0xAF,0xFF},
	{0x5F,0xD7,0x00},{0x5F,0xD7,0x5F},{0x5F,0xD7,0x87},{0x5F,0xD7,0xAF},
	{0x5F,0xD7,0xD7},{0x5F,0xD7,0xFF},{0x5F,0xFF,0x00},{0x5F,0xFF,0x5F},
	{0x5F,0xFF,0x87},{0x5F,0xFF,0xAF},{0x5F,0xFF,0xD7},{0x5F,0xFF,0xFF},
	{0x87,0x00,0x00},{0x87,0x00,0x5F},{0x87,0x00,0x87},{0x87,0x00,0xAF},
	{0x87,0x00,0xD7},{0x87,0x00,0xFF},{0x87,0x5F,0x00},{0x87,0x5F,0x5F},
	{0x87,0x5F,0x87},{0x87,0x5F,0xAF},{0x87,0x5F,0xD7},{0x87,0x5F,0xFF},
	{0x87,0x87,0x00},{0x87,0x87,0x5F},{0x87,0x87,0x87},{0x87,0x87,0xAF},
	{0x87,0x87,0xD7},{0x87,0x87,0xFF},{0x87,0xAF,0x00},{0x87,0xAF,0x5F},
	{0x87,0xAF,0x87},{0x87,0xAF,0xAF},{0x87,0xAF,0xD7},{0x87,0xAF,0xFF},
	{0x87,0xD7,0x00},{0x87,0xD7,0x5F},{0x87,0xD7,0x87},{0x87,0xD7,0xAF},
	{0x87,0xD7,0xD7},{0x87,0xD7,0xFF},{0x87,0xFF,0x00},{0x87,0xFF,0x5F},
	{0x87,0xFF,0x87},{0x87,0xFF,0xAF},{0x87,0xFF,0xD7},{0x87,0xFF,0xFF},
	{0xAF,0x00,0x00},{0xAF,0x00,0x5F},{0xAF,0x00,0x87},{0xAF,0x00,0xAF},
	{0xAF,0x00,0xD7},{0xAF,0x00,0xFF},{0xAF,0x5F,0x00},{0xAF,0x5F,0x5F},
	{0xAF,0x5F,0x87},{0xAF,0x5F,0xAF},{0xAF,0x5F,0xD7},{0xAF,0x5F,0xFF},
	{0xAF,0x87,0x00},{0xAF,0x87,0x5F},{0xAF,0x87,0x87},{0xAF,0x87,0xAF},
	{0xAF,0x87,0xD7},{0xAF,0x87,0xFF},{0xAF,0xAF,0x00},{0xAF,0xAF,0x5F},
	{0xAF,0xAF,0x87},{0xAF,0xAF,0xAF},{0xAF,0xAF,0xD7},{0xAF,0xAF,0xFF},
	{0xAF,0xD7,0x00},{0xAF,0xD7,0x5F},{0xAF,0xD7,0x87},{0xAF,0xD7,0xAF},
	{0xAF,0xD7,0xD7},{0xAF,0xD7,0xFF},{0xAF,0xFF,0x00},{0xAF,0xFF,0x5F},
	{0xAF,0xFF,0x87},{0xAF,0xFF,0xAF},{0xAF,0xFF,0xD7},{0xAF,0xFF,0xFF},
	{0xD7,0x00,0x00},{0xD7,0x00,0x5F},{0xD7,0x00,0x87},{0xD7,0x00,0xAF},
	{0xD7,0x00,0xD7},{0xD7,0x00,0xFF},{0xD7,0x5F,0x00},{0xD7,0x5F,0x5F},
	{0xD7,0x5F,0x87},{0xD7,0x5F,0xAF},{0xD7,0x5F,0xD7},{0xD7,0x5F,0xFF},
	{0xD7,0x87,0x00},{0xD7,0x87,0x5F},{0xD7,0x87,0x87},{0xD7,0x87,0xAF},
	{0xD7,0x87,0xD7},{0xD7,0x87,0xFF},{0xD7,0xAF,0x00},{0xD7,0xAF,0x5F},
	{0xD7,0xAF,0x87},{0xD7,0xAF,0xAF},{0xD7,0xAF,0xD7},{0xD7,0xAF,0xFF},
	{0xD7,0xD7,0x00},{0xD7,0xD7,0x5F},{0xD7,0xD7,0x87},{0xD7,0xD7,0xAF},
	{0xD7,0xD7,0xD7},{0xD7,0xD7,0xFF},{0xD7,0xFF,0x00},{0xD7,0xFF,0x5F},
	{0xD7,0xFF,0x87},{0xD7,0xFF,0xAF},{0xD7,0xFF,0xD7},{0xD7,0xFF,0xFF},
	{0xFF,0x00,0x00},{0xFF,0x00,0x5F},{0xFF,0x00,0x87},{0xFF,0x00,0xAF},
	{0xFF,0x00,0xD7},{0xFF,0x00,0xFF},{0xFF,0x5F,0x00},{0xFF,0x5F,0x5F},
	{0xFF,0x5F,0x87},{0xFF,0x5F,0xAF},{0xFF,0x5F,0xD7},{0xFF,0x5F,0xFF},
	{0xFF,0x87,0x00},{0xFF,0x87,0x5F},{0xFF,0x87,0x87},{0xFF,0x87,0xAF},
	{0xFF,0x87,0xD7},{0xFF,0x87,0xFF},{0xFF,0xAF,0x00},{0xFF,0xAF,0x5F},
	{0xFF,0xAF,0x87},{0xFF,0xAF,0xAF},{0xFF,0xAF,0xD7},{0xFF,0xAF,0xFF},
	{0xFF,0xD7,0x00},{0xFF,0xD7,0x5F},{0xFF,0xD7,0x87},{0xFF,0xD7,0xAF},
	{0xFF,0xD7,0xD7},{0xFF,0xD7,0xFF},{0xFF,0xFF,0x00},{0xFF,0xFF,0x5F},
	{0xFF,0xFF,0x87},{0xFF,0xFF,0xAF},{0xFF,0xFF,0xD7},{0xFF,0xFF,0xFF},
	{0x08,0x08,0x08},{0x12,0x12,0x12},{0x1C,0x1C,0x1C},{0x26,0x26,0x26},
	{0x30,0x30,0x30},{0x3A,0x3A,0x3A},{0x44,0x44,0x44},{0x4E,0x4E,0x4E},
	{0x58,0x58,0x58},{0x62,0x62,0x62},{0x6C,0x6C,0x6C},{0x76,0x76,0x76},
	{0x80,0x80,0x80},{0x8A,0x8A,0x8A},{0x94,0x94,0x94},{0x9E,0x9E,0x9E},
	{0xA8,0xA8,0xA8},{0xB2,0xB2,0xB2},{0xBC,0xBC,0xBC},{0xC6,0xC6,0xC6},
	{0xD0,0xD0,0xD0},{0xDA,0xDA,0xDA},{0xE4,0xE4,0xE4},{0xEE,0xEE,0xEE}
};

// Character set arrays consist of the minimum and maximum parts of the range of
// substituted characters, including, and is followed by the array of
// replacements.

const uint32_t charset_united_kingdom[] = { 0x23, 0x23, 0x20AC };

const uint32_t charset_dec_graphics[] = { 0x5F, 0x7E,
	0x0000, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1,
	0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB,
	0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C, 0x2502, 0x2264,
	0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7 };

// Note 1: Unicode does not seem to have an equivalent to the fractional glyphs
// supported by the VT52 which combined superscripts of 2, 5, and 7 with a
// solidus, though it does have an equivalent to 1-over (U+215A). A hack is
// necessary, so for now we're just using replacement chars for 2/5/7.

// Note 2: bars at odd scanlines are supported by Unicode and can be translated
// directly, but even scanlines are not. Currently, they're currected to the
// line beneath them, but it'd be nice to hack in even scanlines at some point.

const uint32_t charset_vt52_graphics[] = { 0x5E, 0x7E,
	0x0000, 0x0000, 0x2665, 0x2588, 0x215F, 0xFFFD, 0xFFFD, 0xFFFD, 0x00B0,
	0x00B1, 0x2192, 0x2026, 0x00F7, 0x2193, 0x23BA, 0x23BA, 0x23BB, 0x23BB,
	0x2500, 0x2500, 0x23BC, 0x23BC, 0x2080, 0x2081, 0x2082, 0x2083, 0x2084,
	0x2085, 0x2086, 0x2087, 0x2088, 0x2089, 0x00B6 };

const struct cell default_attrs = {
	.background = {0, 0, 0},
	.foreground = {7, 0, 0}
};

struct color palette[256];
bool mode[MODE_COUNT];
struct cursor cursor, saved_cursor;
bool *tabstops;
struct line **lines;
short screen_width, screen_height, scroll_top, scroll_bottom;

void
deinit_screen()
{
	int i;

	for (i = 0; i < screen_height; i++)
		free(lines[i]);

	free(lines);
	free(tabstops);
}

void
resize(int width, int height)
{
	int i;

	deinit_screen();

	if (!(tabstops = calloc(width, sizeof(bool))))
		pdie("failed to allocate tab stop memory");

	for (i = 8; i < width; i += 8)
		tabstops[i] = true;

	if (!(lines = calloc(height, sizeof(struct line *))))
		pdie("failed to allocate line array memory");

	for (i = 0; i < height; i++)
		if (!(lines[i] = calloc(LINE_SIZE(width), 1)))
			pdie("failed to allocate line memory");

	screen_width = width;
	screen_height = height;
	scroll_top = 0;
	scroll_bottom = height - 1;
	cursor.x = 0;
	cursor.y = 0;

	wmresize();
}

void
reset()
{
	int i;

	memcpy(palette, default_palette, sizeof(palette));

	memset(mode, 0, sizeof(mode));
	mode[DECANM] = true;
	mode[DECSCLM] = true;
	mode[DECARM] = true;
	mode[DECINLM] = true;
	mode[DECTCEM] = true;

	memset(&cursor, 0, sizeof(cursor));
	cursor.attrs = default_attrs;

	memset(tabstops, 0, screen_width * sizeof(bool));
	for (i = 8; i < screen_width; i += 8)
		tabstops[i] = true;

	for (i = 0; i < screen_height; i++)
		memset(lines[i], 0, LINE_SIZE(screen_width));

	saved_cursor = cursor;
	scroll_top = 0;
	scroll_bottom = screen_height - 1;
}

void
insert_line()
{
	struct line *temp;
	int i;

	temp = lines[scroll_bottom];

	memmove(&lines[cursor.y + 1], &lines[cursor.y],
		(scroll_bottom - cursor.y) * sizeof(struct line *));

	lines[cursor.y] = temp;

	for (i = 0; i < screen_width; i++)
		lines[cursor.y]->cells[i] = cursor.attrs;
}

void
delete_line()
{
	struct line *temp;
	int i;

	temp = lines[cursor.y];

	memmove(&lines[cursor.y], &lines[cursor.y + 1],
		(scroll_bottom - cursor.y) * sizeof(struct line *));

	lines[scroll_bottom] = temp;

	for (i = 0; i < screen_width; i++)
		lines[scroll_bottom]->cells[i] = cursor.attrs;
}

void
erase_display(int param)
{
	int x, y, n;

	switch (param) {
	case 0:
		if (cursor.x == 0)
			lines[cursor.y]->dimensions = SINGLE_WIDTH;
		erase_line(0);
		y = cursor.y + 1;
		n = screen_height;
		break;
	case 1:
		if (cursor.x == screen_width - 1)
			lines[cursor.y]->dimensions = SINGLE_WIDTH;
		erase_line(1);
		y = 0;
		n = cursor.y;
		break;
	case 2:
		y = 0;
		n = screen_height;
		break;
	default:
		return;
	}

	for (; y < n; y++) {
		lines[y]->dimensions = SINGLE_WIDTH;

		for (x = 0; x < screen_width; x++)
			lines[y]->cells[x] = cursor.attrs;
	}

	cursor.last_column = false;
}

void
erase_line(int param)
{
	int x, max;

	switch (param) {
	case 0: x = cursor.x; max = screen_width; break;
	case 1: x = 0; max = cursor.x + 1; break;
	case 2: x = 0; max = screen_width; break;
	default: return;
	}

	for (; x < max; x++)
		lines[cursor.y]->cells[x] = cursor.attrs;

	cursor.last_column = false;
}

void
warpto(int x, int y)
{
	int miny, maxy;

	miny = mode[DECOM] ? scroll_top : 0;
	maxy = mode[DECOM] ? scroll_bottom : screen_height - 1;

	if (x < 0) x = 0; else if (x >= screen_width) x = screen_width - 1;
	if (y < miny) y = miny; else if (y > maxy) y = maxy;

	cursor.x = x;
	cursor.y = y;
	cursor.last_column = false;
}

void
move_cursor(unsigned char direction, int amount)
{
	switch (direction) {
	case 0x41: warpto(cursor.x, cursor.y - amount); break;
	case 0x42: warpto(cursor.x, cursor.y + amount); break;
	case 0x43: warpto(cursor.x + amount, cursor.y); break;
	case 0x44: warpto(cursor.x - amount, cursor.y); break;
	}
}

void
scrollup()
{
	struct line *temp;

	memset((temp = lines[scroll_top]), 0, LINE_SIZE(screen_width));

	memmove(&lines[scroll_top], &lines[scroll_top + 1],
		(scroll_bottom - scroll_top) * sizeof(struct line *));

	lines[scroll_bottom] = temp;
}

void
scrolldown()
{
	struct line *temp;

	memset((temp = lines[scroll_bottom]), 0, LINE_SIZE(screen_width));

	memmove(&lines[scroll_top + 1], &lines[scroll_top],
		(scroll_bottom - scroll_top) * sizeof(struct line *));

	lines[scroll_top] = temp;
}

void
newline()
{
	cursor.last_column = false;

	if (cursor.y < scroll_bottom)
		cursor.y++;
	else
		scrollup();
}

void
revline()
{
	cursor.last_column = false;

	if (cursor.y > scroll_top)
		warpto(cursor.x, cursor.y - 1);
	else
		scrolldown();
}

void
putch(long ch)
{
	struct cell *cell;
	const uint32_t *charset;
	const unsigned char *glyph;
	int increment;

	if (cursor.last_column) {
		cursor.x = 0;
		newline();
	}

	cell = &lines[cursor.y]->cells[cursor.x];
	*cell = cursor.attrs;

	if (!cursor.conceal) {
		if ((charset = cursor.charset[mode[SHIFT_OUT]]) &&
			ch >= charset[0] && ch <= charset[1])
			ch = charset[ch - charset[0] + 2];

		cell->code_point = ch;
	}

	increment = ch ?
		((glyph = find_glyph(ch)) && glyph[0] == '\2' ? 2 : 1) : 1;

	if (cursor.x + increment >= screen_width) {
		if (mode[DECAWM]) cursor.last_column = true;
	} else {
		cursor.x += increment;
	}
}