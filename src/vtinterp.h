// vtinterp.h - video terminal emulator routines
// Copyright (C) 2018 Megan Ruggiero. All rights reserved.
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

#ifndef VTINTERP_H
#define VTINTERP_H

enum { COLS = 80, ROWS = 24 };
enum { INTENSITY_NORMAL, INTENSITY_BOLD, INTENSITY_FAINT };
enum { BLINK_NONE, BLINK_SLOW, BLINK_FAST };
enum { UNDERLINE_NONE, UNDERLINE_SINGLE, UNDERLINE_DOUBLE };
enum { FRAME_NONE, FRAME_FRAMED, FRAME_ENCIRCLED };

enum {
	LNM,
	DECCKM,
	DECANM,
	DECCOLM,
	DECSCLM,
	DECSCNM,
	DECOM,
	DECAWM,
	DECARM,
	DECINLM,
	DECTCEM,
	MODE_COUNT
};

struct cursor {
	short x, y;
};

struct cell {
	uint32_t	code_point:21;
	uint8_t		font:5;
	uint8_t		intensity:2;
	uint8_t		blink:2;
	uint8_t		underline:2;
	uint8_t		frame:2;
	bool		italic:1;
	bool		negative:1;
	bool		crossed_out:1;
	bool		fraktur:1;
	bool		overline:1;
};

extern struct cursor cursor;
extern struct cell screen[ROWS][COLS];
extern bool mode[MODE_COUNT];
extern bool transmit_disabled;
extern bool keypad_application_mode;

void vtreset(void);
void vtinterp(const unsigned char *, size_t);

#endif