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

#include <EGL/eglplatform.h>

#define CHARWIDTH 8
#define CHARHEIGHT 16

// --- error handling --- //

#define die(message) (errx(EXIT_FAILURE, "%s", message))
#define pdie(message) (err(EXIT_FAILURE, "%s", message))

// --- settings --- //

// Note: instance_name must be non-const because XClassHint declares res_name
// non-const. It can be made const if we stop using XSetClassHint in xlib.c.
extern char *instance_name;
extern const char *answerback;
extern float opacity, glow, static_, glow_line, glow_line_speed;

// --- timing --- //

extern int timer_count;
extern uint64_t current_time;

// --- window management --- //

struct color { uint8_t r, g, b; };

extern int window_width, window_height;

void wminit(void);
void wmkill(void);
void wmpoll(void);
void wmname(const char *);
void wmiconname(const char *);
void wmresize(void);
void wmbell(void);
void wmparsecolor(struct color *, const char *);

// --- rendering --- //

void glinit(EGLNativeDisplayType, EGLNativeWindowType);
void glkill(void);
void gldraw(void);

// --- pseudoterminals --- //

void ptinit(void);
void ptkill(void);
void ptbreak(bool);
void ptwrite(const char *, ...) __attribute__((__format__(printf, 1, 2)));
void ptpump(void);

// --- escape codes --- //

void unrecognized_escape(unsigned char, unsigned char, unsigned char);
void execute(unsigned char);
void vtinterp(unsigned char);
void vt52(long);
void vt100(long);

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

enum {
	UTF8	= 1 <<  0, // Enable UTF-8 interpreter.
	XOFF	= 1 <<  1, // Data transmission disabled by an XOFF octet.
	PAUSED	= 1 <<  2, // Used by xlib.c to track sent XON/XOFF signals.
	AUTOPRINT=1 <<  3, // TODO : Autoprint line on LF, FF, VT, or DECAWM
	VT52GFX	= 1 <<  4, // VT52 Graphic Character Set Enabled
	S8C1T	= 1 <<  5, // Send 8-bit control sequences (TODO : implement)
	LNM	= 1 <<  6, // Line Feed/New Line Mode
	DECKPAM	= 1 <<  7, // Keypad Application Mode
	DECCKM	= 1 <<  8, // Cursor Keys Mode
	DECANM	= 1 <<  9, // ANSI/VT52 Mode
	DECSCLM	= 1 << 10, // Scrolling Mode (TODO : implement)
	DECSCNM	= 1 << 11, // Screen Mode
	DECOM	= 1 << 12, // Origin Mode
	DECAWM	= 1 << 13, // Autowrap Mode
	DECARM	= 1 << 14, // Auto Repeat Mode
	DECINLM	= 1 << 15, // Interlace Mode (TODO : implement)
	DECTCEM	= 1 << 16  // Text Cursor Enable Mode
};

// From the days of yore, when Unicode wasn't available on terminals and
// programmers had to constantly switch between character sets. GL and GR refer
// to the "left" and "right" parts of the glyph table, meaning octets 0-127 and
// 128-255, respecitvely. G0, G1, G2, and G3 are the logical glyph tables. To
// set the glyphs for 0-127, you would first assign the physical glyph table
// (f.e. ISO Latin-1) to a logical glyph table (f.e. G2) and then shift it into
// the right in-use table, GR. It's less complicated than it sounds!
enum { GL, GR };
enum { G0, G1, G2, G3 };

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
	const uint32_t	*logical_charsets[4];
	int		 active_charsets[2];
	short		 x, y;
	bool		 conceal, last_column;
};

#define LINE_SIZE(width) \
	(sizeof(struct line) + (width) * sizeof(struct cell))

// NOTE: instead of charset_ascii we just use NULL.
extern const uint32_t
	charset_united_kingdom[],
	charset_dec_graphics[],
	charset_vt52_graphics[];

extern const struct cell default_attrs;

extern struct color palette[256];
extern long mode;
extern struct cursor cursor, saved_cursor;
extern bool *tabstops;
extern struct line **lines;
extern short screen_width, screen_height, scroll_top, scroll_bottom;

void deinit_screen(void);
void resize(int, int);
void reset(void);
void screen_align(void);
void tab(void);
void insert_line(void);
void delete_line(void);
void erase_display(int);
void erase_line(int);
void warpto(int, int);
void move_cursor(unsigned char, int);
void scrollup(void);
void scrolldown(void);
void newline(void);
void revline(void);
void nextline(void);
void linefeed(void);
void carriagereturn(void);
void print(long);

static inline bool
getmode(long flag)
{
	return mode & flag;
}

static inline bool
setmode(long flag, bool value)
{
	value ? (mode |= flag) : (mode &= ~flag);
	return value;
}

static inline void
setlinea(int dimensions)
{
	lines[cursor.y]->dimensions = dimensions;
}

static inline void
setcharset(int logical_charset, const uint32_t *physical_charset)
{
	cursor.logical_charsets[logical_charset] = physical_charset;
}

static inline void
singleshift(int logical_charset __attribute__((unused)))
{
	warnx("TODO : implement single shifts");
}

static inline void
lockingshift(int active_charset, int logical_charset)
{
	cursor.active_charsets[active_charset] = logical_charset;
}

static inline void
save_cursor()
{
	saved_cursor = cursor;
}

static inline void
restore_cursor()
{
	cursor = saved_cursor;
}

static inline void
settab()
{
	tabstops[cursor.x] = true;
}

#endif // !TERMINIX_H