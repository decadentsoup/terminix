// screen.h - screen management routines
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

#ifndef SCREEN_H
#define SCREEN_H

// DOUBLE_HEIGHT_* must come after DOUBLE_WIDTH for render_glyph() to work.
enum { SINGLE_WIDTH, DOUBLE_WIDTH, DOUBLE_HEIGHT_TOP, DOUBLE_HEIGHT_BOTTOM };

enum { INTENSITY_NORMAL, INTENSITY_BOLD, INTENSITY_FAINT };
enum { BLINK_NONE, BLINK_SLOW, BLINK_FAST };
enum { UNDERLINE_NONE, UNDERLINE_SINGLE, UNDERLINE_DOUBLE };
enum { FRAME_NONE, FRAME_FRAMED, FRAME_ENCIRCLED };

enum {
	TRANSMIT_DISABLED,
	DECKPAM,
	LNM,
	DECCKM,
	DECANM,
	DECSCLM,
	DECSCNM,
	DECOM,
	DECAWM,
	DECARM,
	DECINLM,
	DECTCEM,
	MODE_COUNT
};

struct color {
	unsigned char r, g, b;
};

struct cell {
	struct color	background, foreground;
	uint32_t	code_point:21;
	uint8_t		font:4;
	uint8_t		intensity:2;
	uint8_t		blink:2;
	uint8_t		underline:2;
	uint8_t		frame:2;
	bool		italic:1;
	bool		negative:1;
	bool		crossed_out:1;
	bool		fraktur:1;
	bool		overline:1;
	bool		bg_truecolor:1;
	bool		fg_truecolor:1;
};

struct line {
	char		dimensions;
	struct cell	cells[];
};

struct cursor {
	struct cell	attrs;
	short		x, y;
	bool		conceal, last_column;
};

#define LINE_SIZE(width) \
	(sizeof(struct line) + (width) * sizeof(struct cell))

extern const struct cell default_attrs;
extern struct color palette[256];
extern bool mode[MODE_COUNT];
extern struct cursor cursor, saved_cursor;
extern bool *tabstops;
extern struct line **lines;
extern short screen_width, screen_height, scroll_top, scroll_bottom;

void deinit_screen(void);
void resize(int, int);
void reset(void);
void warpto(int, int);
void scrollup(void);
void scrolldown(void);
void newline(void);
void putch(long);

#endif