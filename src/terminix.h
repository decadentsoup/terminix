// terminix.h - common headers, structures, variables, and routines
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

#ifndef TERMINIX_H
#define TERMINIX_H

#include <err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- error handling --- //

#define die(message) (errx(EXIT_FAILURE, "%s", message))
#define pdie(message) (err(EXIT_FAILURE, "%s", message))

// --- user interface --- //

void set_title(const char *);

// --- pseudoterminals --- //

void init_ptmx(const char *);
void deinit_ptmx(void);
void write_ptmx_num(unsigned int);
void write_ptmx(const unsigned char *, size_t);
void pump_ptmx(void);

// --- escape codes --- //

void vtinterp(const unsigned char *, size_t);

// --- unifont --- //

extern const unsigned char *const plane0and1[];
extern const unsigned char *const plane15[];
static inline const unsigned char *
find_glyph(long code_point)
{
	if (code_point >= 0x000000 && code_point <= 0x01FFFF)
		return plane0and1[code_point];

	if (code_point >= 0x0F0000 && code_point <= 0x0FFFFF)
		return plane15[code_point - 0x0F0000];

	return 0;
}

// --- screen management --- //

// DOUBLE_HEIGHT_* must come after DOUBLE_WIDTH for render_glyph() to work.
enum { SINGLE_WIDTH, DOUBLE_WIDTH, DOUBLE_HEIGHT_TOP, DOUBLE_HEIGHT_BOTTOM };

enum { INTENSITY_NORMAL, INTENSITY_BOLD, INTENSITY_FAINT };
enum { BLINK_NONE, BLINK_SLOW, BLINK_FAST };
enum { UNDERLINE_NONE, UNDERLINE_SINGLE, UNDERLINE_DOUBLE };
enum { FRAME_NONE, FRAME_FRAMED, FRAME_ENCIRCLED };

enum { TRANSMIT_DISABLED, SHIFT_OUT, DECKPAM, LNM, DECCKM, DECANM, DECSCLM,
	DECSCNM, DECOM, DECAWM, DECARM, DECINLM, DECTCEM, MODE_COUNT };

struct color { uint8_t r, g, b; };

struct cell {
	struct color	background, foreground;
	uint32_t	code_point:21;
	uint8_t		font:4, intensity:2, blink:2, underline:2, frame:2;
	bool		italic:1, negative:1, crossed_out:1, fraktur:1,
			overline:1, bg_truecolor:1, fg_truecolor:1;
};

struct line {
	char		dimensions;
	struct cell	cells[];
};

struct cursor {
	struct cell	 attrs;
	const uint32_t	*charset[2];
	short		 x, y;
	bool		 conceal, last_column;
};

#define LINE_SIZE(width) \
	(sizeof(struct line) + (width) * sizeof(struct cell))

extern const uint32_t charset_united_kingdom[], charset_dec_graphics[];
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
void revline(void);
void putch(long);

#endif // !TERMINIX_H