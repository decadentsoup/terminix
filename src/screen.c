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

#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "screen.h"
#include "unifont.h"

#define pdie(message) (err(EXIT_FAILURE, "%s", message))

int screen_width, screen_height, scroll_top, scroll_bottom;
struct cursor cursor, saved_cursor;
struct line *lines;
struct cell *screen;
bool *tabstops;
bool mode[MODE_COUNT];

void
deinit_screen()
{
	free(lines);
	free(screen);
	free(tabstops);
}

void
resize(int width, int height)
{
	int i;

	deinit_screen();

	if (!(lines = calloc(height, sizeof(struct line))))
		pdie("failed to allocate line memory");

	if (!(screen = calloc(width * height, sizeof(struct cell))))
		pdie("failed to allocate screen memory");

	if (!(tabstops = calloc(width, sizeof(bool))))
		pdie("failed to allocate tab stop memory");

	for (i = 8; i < width; i += 8)
		tabstops[i] = true;

	screen_width = width;
	screen_height = height;
	scroll_top = 0;
	scroll_bottom = screen_height - 1;
	cursor.x = 0;
	cursor.y = 0;
}

void
reset()
{
	int i;

	memset(&cursor, 0, sizeof(cursor));
	memset(lines, 0, screen_height * sizeof(struct line));
	memset(screen, 0, screen_width * screen_height * sizeof(struct cell));

	memset(mode, 0, sizeof(mode));
	mode[DECANM] = true;
	mode[DECSCLM] = true;
	mode[DECARM] = true;
	mode[DECINLM] = true;
	mode[DECTCEM] = true;

	memset(tabstops, 0, screen_width * sizeof(bool));
	for (i = 8; i < screen_width; i += 8)
		tabstops[i] = true;

	scroll_top = 0;
	scroll_bottom = screen_height - 1;
	saved_cursor = cursor;
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
scrollup()
{
	memmove(&lines[scroll_top], &lines[scroll_top + 1],
		(scroll_bottom - scroll_top) * sizeof(struct line));

	memset(&lines[scroll_bottom], 0, sizeof(struct line));

	memmove(&screen[scroll_top * screen_width],
		&screen[(scroll_top + 1) * screen_width],
		screen_width * (scroll_bottom - scroll_top) * sizeof(struct cell));

	memset(&screen[screen_width * scroll_bottom], 0,
		screen_width * sizeof(struct cell));
}

void
scrolldown()
{
	memmove(&lines[scroll_top + 1], &lines[scroll_top],
		(scroll_bottom - scroll_top) * sizeof(struct line));

	memset(&lines[scroll_top], 0, sizeof(struct line));

	memmove(&screen[(scroll_top + 1) * screen_width],
		&screen[scroll_top * screen_width],
		screen_width * (scroll_bottom - scroll_top) * sizeof(struct cell));

	memset(&screen[screen_width * scroll_top], 0,
		screen_width * sizeof(struct cell));
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
putch(long ch)
{
	struct cell *cell;
	const unsigned char *glyph;
	int increment;

	if (cursor.last_column) {
		cursor.x = 0;
		newline();
	}

	// TODO : check behavior on unevenly wide screens
	cell = &screen[(cursor.x / (lines[cursor.y].dimensions ? 2 : 1)) +
		cursor.y * screen_width];

	*cell = cursor.attrs;

	if (!cursor.conceal)
		cell->code_point = ch;

	increment = ((glyph = find_glyph(ch)) && glyph[0] == '\2' ? 2 : 1) +
		(lines[cursor.y].dimensions ? 1 : 0);

	if (cursor.x + increment >= screen_width) {
		if (mode[DECAWM]) cursor.last_column = true;
	} else {
		cursor.x += increment;
	}
}