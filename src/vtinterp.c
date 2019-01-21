// vtinterp.c - video terminal emulator routines
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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "ptmx.h"
#include "vtinterp.h"

#include <err.h>//TODO

#define MAX_PARAMETERS 16
#define PARAMETER_MAX 65535

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
struct cell screen[ROWS][COLS];
bool mode[MODE_COUNT];
bool transmit_disabled;
bool keypad_application_mode;

// VT100 with Processor Option, Advanced Video Option, and Graphics Option
static const unsigned char DEVICE_ATTRS[] =
	{ 0x1B, 0x5B, 0x3F, 0x31, 0x3B, 0x37, 0x63 };

static enum state state;

static unsigned char intermediate;
static bool intermediate_bad;

static unsigned short parameters[MAX_PARAMETERS];
static unsigned char parameter_index;

static struct cell attrs;
static bool conceal;

static void interpret(unsigned char);
static void print(long);
static void execute(unsigned char);
static void newline(void);
static void collect(unsigned char);
static void param(unsigned char);
static void esc_dispatch(unsigned char);
static void csi_dispatch(unsigned char);
static void move_cursor(unsigned char);
static void erase_display(void);
static void erase_line(void);
static void set_mode(bool);
static void select_graphic_rendition(void);

void
vtreset()
{
	cursor.x = 0;
	cursor.y = 0;

	memset(screen, 0, sizeof(screen));

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
	screen[cursor.y][cursor.x] = attrs;

	if (!conceal)
		screen[cursor.y][cursor.x].code_point = ch;

	if (cursor.x < COLS - 1) {
		cursor.x++;
	} else if (cursor.x == COLS - 1 && mode[DECAWM]) {
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
newline()
{
	if (++cursor.y >= ROWS) {
		cursor.y--;
		memmove(&screen[0], &screen[1], sizeof(screen[0]) * (ROWS - 1));
		memset(&screen[ROWS - 1], 0, sizeof(screen[0]));
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

	if (intermediate != 0) {
		warnx("esc_dispatch(collect=%c final=%c", intermediate, byte);
		return;
	}

	switch (byte) {
	case 0x3D:
		keypad_application_mode = true;
		break;
	case 0x3E:
		keypad_application_mode = false;
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
		move_cursor(byte);
		break;
	case 0x48:
		// TODO : DECOM

		if (parameters[0] > ROWS) parameters[0] = ROWS;
		if (parameters[1] > COLS) parameters[1] = COLS;

		cursor.y = parameters[0] ? parameters[0] - 1 : 0;
		cursor.x = parameters[1] ? parameters[1] - 1 : 0;

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
	default:
		warnx("unrecognized CSI: %c/%x", byte, byte);
		break;
	}
}

static void
move_cursor(unsigned char byte)
{
	int i;

	if (parameters[0] == 0)
		parameters[0] = 1;

	for (i = 0; i < parameters[0]; i++)
		switch (byte) {
		case 0x41:
			if (cursor.y > 0) cursor.y--;
			break;
		case 0x42:
			if (cursor.y < ROWS - 1) cursor.y++;
			break;
		case 0x43:
			if (cursor.x < COLS - 1) cursor.x++;
			break;
		case 0x44:
			if (cursor.x > 0) cursor.x--;
			break;
		}
}

static void
erase_display()
{
	int x, y;

	switch (parameters[0]) {
	case 0:
		for (y = cursor.y; y < ROWS; y++)
			for (x = cursor.x; x < COLS; x++)
				screen[y][x] = attrs;
		break;
	case 1:
		for (y = 0; y < ROWS; y++)
			for (x = 0; x < COLS; x++) {
				screen[y][x] = attrs;
				if (x == cursor.x && y == cursor.y) return;
			}
		break;
	case 2:
		for (y = 0; y < ROWS; y++)
			for (x = 0; x < COLS; x++)
				screen[y][x] = attrs;
		break;
	}
}

static void
erase_line()
{
	int x, max;

	switch (parameters[0]) {
	case 0: x = cursor.x; max = COLS; break;
	case 1: x = 0; max = cursor.x + 1; break;
	case 2: x = 0; max = COLS; break;
	}

	for (; x < max; x++)
		screen[cursor.y][x] = attrs;
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
			case 2: mode[DECANM] = value; break;
			case 3: mode[DECCOLM] = value; break;
			case 4: mode[DECSCLM] = value; break;
			case 5: mode[DECSCNM] = value; break;
			case 6: mode[DECOM] = value; break;
			case 7: mode[DECAWM] = value; break;
			case 8: mode[DECARM] = value; break;
			case 9: mode[DECINLM] = value; break;
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