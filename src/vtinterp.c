// vtinterp.c - video terminal emulator routines
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
#include "ptmx.h"
#include "vtinterp.h"

#define MAX_PARAMETERS 16
#define PARAMETER_MAX 16383

#define pdie(message) (err(EXIT_FAILURE, "%s", message))

enum state {
	STATE_GROUND,
	STATE_ESCAPE,
	STATE_ESCAPE_INTERMEDIATE,
	STATE_CSI_ENTRY,
	STATE_CSI_PARAM,
	STATE_CSI_INTERMEDIATE,
	STATE_CSI_IGNORE,
	STATE_DCS_ENTRY,
	STATE_DCS_PARAM,
	STATE_DCS_INTERMEDIATE,
	STATE_DCS_PASSTHROUGH,
	STATE_DCS_IGNORE,
	STATE_OSC_STRING,
	STATE_SOS_STRING,
	STATE_PM_STRING,
	STATE_APC_STRING
};

struct cursor cursor;
struct cell *screen;
int screen_width, screen_height;
bool mode[MODE_COUNT];
bool transmit_disabled;
bool keypad_application_mode;

// VT100 with Processor Option, Advanced Video Option, and Graphics Option
static const unsigned char DEVICE_ATTRS[] =
	{ 0x1B, 0x5B, 0x3F, 0x31, 0x3B, 0x37, 0x63 };

static int scroll_top, scroll_bottom;

static enum state state;

static unsigned char intermediate;
static bool intermediate_bad;

static unsigned short parameters[MAX_PARAMETERS];
static unsigned char parameter_index;

static struct cell attrs;
static bool conceal;

static void warp(int, int);
static void warpto(int, int);
static void scrolldown(void);
static void newline(void);

static void interpret(unsigned char);
static void print(long);
static void execute(unsigned char);
static void collect(unsigned char);
static void param(unsigned char);
static void esc_dispatch(unsigned char);
static void esc_dispatch_private(unsigned char);
static void csi_dispatch(unsigned char);
static void erase_display(void);
static void erase_line(void);
static void set_mode(bool);
static void select_graphic_rendition(void);

void
vtcleanup()
{
	free(screen);
}

void
vtresize(int columns, int rows)
{
	vtcleanup();

	if (!(screen = calloc(columns * rows, sizeof(struct cell *))))
		pdie("failed to allocate memory");

	screen_width = columns;
	screen_height = rows;
	scroll_top = 0;
	scroll_bottom = screen_height - 1;

	if (cursor.x > columns - 1)
		cursor.x = columns - 1;

	if (cursor.y > rows - 1)
		cursor.y = rows - 1;
}

void
vtreset()
{
	cursor.x = 0;
	cursor.y = 0;

	memset(screen, 0, screen_width * screen_height * sizeof(struct cell));

	mode[LNM] = false;
	mode[DECCKM] = false;
	mode[DECANM] = true;
	mode[DECCOLM] = false;
	mode[DECSCLM] = true;
	mode[DECSCNM] = false;
	mode[DECOM] = false;
	mode[DECAWM] = false;
	mode[DECARM] = true;
	mode[DECINLM] = true;
	mode[DECTCEM] = true;

	transmit_disabled = false;
	keypad_application_mode = false;

	scroll_top = 0;
	scroll_bottom = screen_height - 1;
}

static void
warp(int dx, int dy)
{
	warpto(cursor.x + dx, cursor.y + dy);
}

static void
warpto(int x, int y)
{
	int top, bottom;

	top = mode[DECOM] ? scroll_top : 0;
	bottom = mode[DECOM] ? scroll_bottom : screen_height - 1;

	if (x < 0) x = 0; else if (x >= screen_width) x = screen_width - 1;
	if (y < top) y = top; else if (y > bottom) y = bottom;

	cursor.x = x;
	cursor.y = y;
}

static void
scrolldown(void)
{
	memmove(&screen[(scroll_top + 1) * screen_width],
		&screen[scroll_top * screen_width],
		screen_width * (scroll_bottom - scroll_top) * sizeof(struct cell));

	memset(screen, 0, screen_width * sizeof(struct cell));
}

static void
newline()
{
	if (cursor.y < scroll_bottom) {
		cursor.y++;
		return;
	}

	memmove(&screen[scroll_top * screen_width],
		&screen[(scroll_top + 1) * screen_width],
		screen_width * (scroll_bottom - scroll_top) * sizeof(struct cell));

	memset(&screen[screen_width * scroll_bottom], 0,
		screen_width * sizeof(struct cell));
}

void
vtinterp(const unsigned char *buffer, size_t bufsize)
{
	size_t i;

	for (i = 0; i < bufsize; i++)
		interpret(buffer[i]);
}

#define COND(condition, action) \
	do { if (condition) { action; return; } } while(0);

static void
interpret(unsigned char byte)
{
	// substitute and cancel controls
	if (byte == 0x18 || byte == 0x1A) {
		state = STATE_GROUND;
		print(0xFFFD);
		return;
	}

	// escape control
	if (byte == 0x1B) {
		state = STATE_ESCAPE;
		return;
	}

	switch (state) {
	case STATE_GROUND:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x7E, print(byte));
		break;
	case STATE_ESCAPE:
		intermediate = 0;
		intermediate_bad = false;

		parameter_index = 0;
		memset(parameters, 0, sizeof(parameters));

		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte); state = STATE_ESCAPE_INTERMEDIATE);
		COND(byte == 0x50, state = STATE_DCS_ENTRY);
		COND(byte == 0x58, state = STATE_SOS_STRING);
		COND(byte == 0x5B, state = STATE_CSI_ENTRY);
		COND(byte == 0x5D, state = STATE_OSC_STRING);
		COND(byte == 0x5E, state = STATE_PM_STRING);
 		COND(byte == 0x5F, state = STATE_APC_STRING);
		COND(byte <= 0x7E, esc_dispatch(byte));
		break;
	case STATE_ESCAPE_INTERMEDIATE:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte));
		COND(byte <= 0x7E, esc_dispatch(byte));
		break;
	case STATE_CSI_ENTRY:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte); state = STATE_CSI_INTERMEDIATE);
		COND(byte == 0x3A, state = STATE_CSI_IGNORE);
		COND(byte <= 0x3B, param(byte); state = STATE_CSI_PARAM);
		COND(byte <= 0x3F, collect(byte); state = STATE_CSI_PARAM);
		COND(byte <= 0x7E, csi_dispatch(byte));
		break;
	case STATE_CSI_PARAM:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte); state = STATE_CSI_INTERMEDIATE);
		COND(byte == 0x3A, state = STATE_CSI_IGNORE);
		COND(byte <= 0x3B, param(byte));
		COND(byte <= 0x3F, state = STATE_CSI_IGNORE);
		COND(byte <= 0x7E, csi_dispatch(byte));
		break;
	case STATE_CSI_INTERMEDIATE:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte));
		COND(byte <= 0x3F, state = STATE_CSI_IGNORE);
		COND(byte <= 0x7E, csi_dispatch(byte));
		break;
	case STATE_CSI_IGNORE:
		COND(byte <= 0x1F, execute(byte));
		COND(byte >= 0x40 && byte <= 0x7E, state = STATE_GROUND);
		break;
	case STATE_DCS_ENTRY:
		warnx("TODO : STATE_DCS_ENTRY");
		break;
	case STATE_DCS_PARAM:
		warnx("TODO : STATE_DCS_PARAM");
		break;
	case STATE_DCS_INTERMEDIATE:
		warnx("TODO : STATE_DCS_INTERMEDIATE");
		break;
	case STATE_DCS_PASSTHROUGH:
		warnx("TODO : STATE_DCS_PASSTHROUGH");
		break;
	case STATE_DCS_IGNORE:
		warnx("TODO : STATE_DCS_IGNORE");
		break;
	case STATE_OSC_STRING:
	case STATE_SOS_STRING:
	case STATE_PM_STRING:
	case STATE_APC_STRING:
		// ignore everything
		break;
	}
}

static void
print(long ch)
{
	struct cell *cell;

	cell = &screen[cursor.x + cursor.y * screen_width];
	*cell = attrs;

	if (!conceal)
		cell->code_point = ch;

	if (cursor.x < screen_width - 1) {
		cursor.x++;
	} else if (cursor.x == screen_width - 1 && mode[DECAWM]) {
		cursor.x = 0;
		newline();
	}
}

static void
execute(unsigned char byte)
{
	switch (byte) {
	case 0x05: // Enquiry
		warnx("TODO : Enquiry");
		break;
	case 0x07: // Bell
		warnx("TODO : Bell");
		break;
	case 0x08: // Backspace
		if (cursor.x > 0) cursor.x--;
		break;
	case 0x09: // Horizontal Tab
		warnx("TODO : Horizontal Tab");
		break;
	case 0x0A: // Line Feed
	case 0x0B: // Vertical Tab
	case 0x0C: // Form Feed
		newline();
		break;
	case 0x0D: // Carriage Return
		cursor.x = 0;
		break;
	case 0x0E: // Shift Out
		warnx("TODO : Shift In -- G1 character set");
		break;
	case 0x0F: // Shift In
		warnx("TODO : Shift Out -- G0 character set");
		break;
	case 0x11: // Device Control 1 - XON
		transmit_disabled = false;
		break;
	case 0x13: // Device Control 3 - XOFF
		transmit_disabled = true;
		break;
	}
}

static void
collect(unsigned char byte)
{
	if (intermediate)
		intermediate_bad = true;
	else
		intermediate = byte;
}

static void
param(unsigned char byte)
{
	unsigned long param;

	if (parameter_index == MAX_PARAMETERS)
		return;

	if (byte == ';') {
		parameter_index++;
		return;
	}

	param = parameters[parameter_index];
	param = param * 10 + (byte - 0x30);
	if (param > PARAMETER_MAX) param = PARAMETER_MAX;
	parameters[parameter_index] = param;
}

static void
esc_dispatch(unsigned char byte)
{
	state = STATE_GROUND;

	if (intermediate == '#') {
		esc_dispatch_private(byte);
		return;
	}

	if (intermediate)
		return;

	switch (byte) {
	case 0x3D:
		keypad_application_mode = true;
		break;
	case 0x3E:
		keypad_application_mode = false;
		break;
	case 0x44:
		newline();
		break;
	case 0x45:
		cursor.x = 0;
		newline();
		break;
	case 0x4D:
		if (cursor.y > 0)
			cursor.y--;
		else
			scrolldown();
		break;
	default:
		warnx("esc_dispatch(final=%x/%c)", byte, byte);
		break;
	}
}

static void
esc_dispatch_private(unsigned char byte)
{
	int i;

	switch (byte) {
	case 0x38:
		memset(screen, 0, screen_width * screen_height * sizeof(struct cell));
		for (i = 0; i < screen_width * screen_height; i++)
			screen[i].code_point = 0x45;
		break;
	default:
		warnx("esc_dispatch_private(final=%x/%c)", byte, byte);
		break;
	}
}

static void
csi_dispatch(unsigned char byte)
{
	state = STATE_GROUND;

	if (intermediate_bad)
		return;

	if (parameter_index == MAX_PARAMETERS)
		parameter_index = MAX_PARAMETERS - 1;

	switch (byte) {
	case 0x41: case 0x42: case 0x43: case 0x44:
		if (!parameters[0]) parameters[0] = 1;

		switch (byte) {
		case 0x41: warp(0, -parameters[0]); break;
		case 0x42: warp(0, +parameters[0]); break;
		case 0x43: warp(+parameters[0], 0); break;
		case 0x44: warp(-parameters[0], 0); break;
		}

		break;
	case 0x48: case 0x66:
		if (parameters[0] > screen_height) parameters[0] = screen_height;
		if (parameters[1] > screen_width) parameters[1] = screen_width;

		warpto(parameters[1] - 1, parameters[0] - 1);

		break;
	case 0x4A:
		erase_display();
		break;
	case 0x4B:
		erase_line();
		break;
	case 0x63:
		if (parameters[0] == 0)
			write_ptmx(DEVICE_ATTRS, sizeof(DEVICE_ATTRS));
		break;
	case 0x68:
		set_mode(true);
		break;
	case 0x6C:
		set_mode(false);
		break;
	case 0x6D:
		select_graphic_rendition();
		break;
	case 0x72:
		if (!parameters[0]) parameters[0] = 1;
		if (!parameters[1] || parameters[1] > screen_height)
			parameters[1] = screen_height;

		if (parameters[0] < parameters[1]) {
			scroll_top = parameters[0] - 1;
			scroll_bottom = parameters[1] - 1;
			warpto(0, 0);
		}
		break;
	default:
		warnx("unrecognized CSI: %c/%x", byte, byte);
		break;
	}
}

static void
erase_display()
{
	int x, y;

	switch (parameters[0]) {
	case 0:
		for (y = cursor.y; y < screen_height; y++)
			for (x = cursor.x; x < screen_width; x++)
				screen[x + y * screen_width] = attrs;
		break;
	case 1:
		for (y = 0; y < screen_height; y++)
			for (x = 0; x < screen_width; x++) {
				screen[x + y * screen_width] = attrs;
				if (x == cursor.x && y == cursor.y) return;
			}
		break;
	case 2:
		for (x = 0; x < screen_width * screen_height; x++)
			screen[x] = attrs;
		break;
	}
}

static void
erase_line()
{
	int x, max;

	switch (parameters[0]) {
	case 0: x = cursor.x; max = screen_width; break;
	case 1: x = 0; max = cursor.x + 1; break;
	case 2: x = 0; max = screen_width; break;
	}

	for (; x < max; x++)
		screen[x + cursor.y * screen_width] = attrs;
}

static void
set_mode(bool value)
{
	int i;

	for (i = 0; i <= parameter_index; i++)
		if (intermediate == 0x00) {
			if (parameters[i] == 20)
				mode[LNM] = value;
			else
				warnx("i=%c p=%i v=%i", intermediate, parameters[0], value);
		} else if (intermediate == 0x3F) {
			switch (parameters[i]) {
			case 1: mode[DECCKM] = value; break;
			// case 2: mode[DECANM] = value; break;
			// case 3: mode[DECCOLM] = value; break;
			// case 4: mode[DECSCLM] = value; break;
			// case 5: mode[DECSCNM] = value; break;
			case 6: mode[DECOM] = value; warpto(0, 0); break;
			case 7: mode[DECAWM] = value; break;
			case 8: mode[DECARM] = value; break;
			// case 9: mode[DECINLM] = value; break;
			case 25: mode[DECTCEM] = value; break;
			default: warnx("i=%c p=%i v=%i", intermediate, parameters[0], value); break;
			}
		}
}

static void
select_graphic_rendition()
{
	int i;

	if (intermediate)
		return;

	for (i = 0; i <= parameter_index; i++)
		switch (parameters[i]) {
		case 0:
			attrs.font = 0;
			attrs.intensity = INTENSITY_NORMAL;
			attrs.blink = BLINK_NONE;
			attrs.underline = UNDERLINE_NONE;
			attrs.frame = FRAME_NONE;
			attrs.italic = false;
			attrs.negative = false;
			attrs.crossed_out = false;
			attrs.fraktur = false;
			attrs.overline = false;
			conceal = false;
			break;
		case 1: attrs.intensity = INTENSITY_BOLD; break;
		case 2: attrs.intensity = INTENSITY_FAINT; break;
		case 3: attrs.italic = true; break;
		case 4: attrs.underline = UNDERLINE_SINGLE; break;
		case 5: attrs.blink = BLINK_SLOW; break;
		case 6: attrs.blink = BLINK_FAST; break;
		case 7: attrs.negative = true; break;
		case 8: conceal = true; break;
		case 9: attrs.crossed_out = true; break;
		case 10: attrs.font = 0; break;
		case 11: attrs.font = 1; break;
		case 12: attrs.font = 2; break;
		case 13: attrs.font = 3; break;
		case 14: attrs.font = 4; break;
		case 15: attrs.font = 5; break;
		case 16: attrs.font = 6; break;
		case 17: attrs.font = 7; break;
		case 18: attrs.font = 8; break;
		case 19: attrs.font = 9; break;
		case 20: attrs.fraktur = true; break;
		case 21: attrs.underline = UNDERLINE_DOUBLE; break;
		case 22: attrs.intensity = INTENSITY_NORMAL; break;
		case 23: attrs.italic = false; attrs.fraktur = false; break;
		case 24: attrs.underline = UNDERLINE_NONE; break;
		case 25: attrs.blink = BLINK_NONE; break;
		case 27: attrs.negative = false; break;
		case 28: conceal = false; break;
		case 29: attrs.crossed_out = false; break;
		case 51: attrs.frame = FRAME_FRAMED; break;
		case 52: attrs.frame = FRAME_ENCIRCLED; break;
		case 53: attrs.overline = true; break;
		case 54: attrs.frame = FRAME_NONE; break;
		case 55: attrs.overline = false; break;
		}
}