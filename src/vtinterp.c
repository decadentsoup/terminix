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

static bool *tabstops;
static int scroll_top, scroll_bottom;
static struct cursor saved_cursor;

static enum state state;
static unsigned char intermediate;
static unsigned short parameters[MAX_PARAMETERS];
static unsigned char parameter_index;

// VT100 with Processor Option, Advanced Video Option, and Graphics Option
static const unsigned char DEVICE_ATTRS[] =
	{ 0x1B, 0x5B, 0x3F, 0x31, 0x3B, 0x37, 0x63 };

static void warpto(int, int);
static void scrollup(void);
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
static void csi_dispatch_private(unsigned char);
static void erase_display(void);
static void erase_line(void);
static void delete_character(void);
static void device_status_report(void);
static void set_mode(bool);
static void select_graphic_rendition(void);

void
vtcleanup()
{
	free(screen);
	free(tabstops);
}

void
vtresize(int columns, int rows)
{
	int i;

	vtcleanup();

	if (!(screen = calloc(columns * rows, sizeof(struct cell))))
		pdie("failed to allocate screen memory");

	if (!(tabstops = calloc(columns, sizeof(bool))))
		pdie("failed to allocate tabstop memory");

	for (i = 8; i < columns; i += 8)
		tabstops[i] = true;

	screen_width = columns;
	screen_height = rows;
	scroll_top = 0;
	scroll_bottom = screen_height - 1;
	cursor.x = 0;
	cursor.y = 0;
}

void
vtreset()
{
	int i;

	memset(&cursor, 0, sizeof(cursor));
	memset(screen, 0, screen_width * screen_height * sizeof(struct cell));

	mode[TRANSMIT_DISABLED] = false;
	mode[DECKPAM] = false;
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

	memset(tabstops, 0, screen_width * sizeof(bool));
	for (i = 8; i < screen_width; i += 8)
		tabstops[i] = true;

	scroll_top = 0;
	scroll_bottom = screen_height - 1;
	saved_cursor = cursor;
}

static void
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

static void
scrollup()
{
	memmove(&screen[scroll_top * screen_width],
		&screen[(scroll_top + 1) * screen_width],
		screen_width * (scroll_bottom - scroll_top) * sizeof(struct cell));

	memset(&screen[screen_width * scroll_bottom], 0,
		screen_width * sizeof(struct cell));
}

static void
scrolldown()
{
	memmove(&screen[(scroll_top + 1) * screen_width],
		&screen[scroll_top * screen_width],
		screen_width * (scroll_bottom - scroll_top) * sizeof(struct cell));

	memset(&screen[screen_width * scroll_top], 0,
		screen_width * sizeof(struct cell));
}

static void
newline()
{
	cursor.last_column = false;

	if (cursor.y < scroll_bottom)
		cursor.y++;
	else
		scrollup();
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

#define NEXT(target) (state = STATE_##target)

static void
interpret(unsigned char byte)
{
	// substitute and cancel controls
	if (byte == 0x18 || byte == 0x1A) {
		NEXT(GROUND);
		print(0xFFFD);
		return;
	}

	// escape control
	if (byte == 0x1B) {
		NEXT(ESCAPE);
		return;
	}

	switch (state) {
	case STATE_GROUND:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x7E, print(byte));
		break;
	case STATE_ESCAPE:
		intermediate = 0;
		parameter_index = 0;
		memset(parameters, 0, sizeof(parameters));

		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte); NEXT(ESCAPE_INTERMEDIATE));
		COND(byte == 0x50, NEXT(DCS_ENTRY));
		COND(byte == 0x58, NEXT(SOS_STRING));
		COND(byte == 0x5B, NEXT(CSI_ENTRY));
		COND(byte == 0x5D, NEXT(OSC_STRING));
		COND(byte == 0x5E, NEXT(PM_STRING));
 		COND(byte == 0x5F, NEXT(APC_STRING));
		COND(byte <= 0x7E, esc_dispatch(byte));
		break;
	case STATE_ESCAPE_INTERMEDIATE:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte));
		COND(byte <= 0x7E, esc_dispatch(byte));
		break;
	case STATE_CSI_ENTRY:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte); NEXT(CSI_INTERMEDIATE));
		COND(byte == 0x3A, NEXT(CSI_IGNORE));
		COND(byte <= 0x3B, param(byte); NEXT(CSI_PARAM));
		COND(byte <= 0x3F, collect(byte); NEXT(CSI_PARAM));
		COND(byte <= 0x7E, csi_dispatch(byte));
		break;
	case STATE_CSI_PARAM:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte); NEXT(CSI_INTERMEDIATE));
		COND(byte == 0x3A, NEXT(CSI_IGNORE));
		COND(byte <= 0x3B, param(byte));
		COND(byte <= 0x3F, NEXT(CSI_IGNORE));
		COND(byte <= 0x7E, csi_dispatch(byte));
		break;
	case STATE_CSI_INTERMEDIATE:
		COND(byte <= 0x1F, execute(byte));
		COND(byte <= 0x2F, collect(byte));
		COND(byte <= 0x3F, NEXT(CSI_IGNORE));
		COND(byte <= 0x7E, csi_dispatch(byte));
		break;
	case STATE_CSI_IGNORE:
		COND(byte <= 0x1F, execute(byte));
		COND(byte >= 0x40 && byte <= 0x7E, NEXT(GROUND));
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

	if (cursor.last_column) {
		cursor.x = 0;
		newline();
		cursor.last_column = false;//XXX
	}

	cell = &screen[cursor.x + cursor.y * screen_width];
	*cell = cursor.attrs;

	if (!cursor.conceal)
		cell->code_point = ch;

	if (cursor.x < screen_width - 1)
		cursor.x++;
	else if (cursor.x == screen_width - 1 && mode[DECAWM])
		cursor.last_column = true;
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
		if (cursor.x > 0) {
			cursor.x--;
			cursor.last_column = false;
		}
		break;
	case 0x09: // Horizontal Tab
		for (cursor.x++; cursor.x < screen_width && !tabstops[cursor.x];
			cursor.x++);
		if (cursor.x >= screen_width) cursor.x = screen_width - 1;
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
		mode[TRANSMIT_DISABLED] = false;
		break;
	case 0x13: // Device Control 3 - XOFF
		mode[TRANSMIT_DISABLED] = true;
		break;
	}
}

static void
collect(unsigned char byte)
{
	intermediate = intermediate ? 0xFF : byte;
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
	NEXT(GROUND);

	if (intermediate == 0x23) {
		esc_dispatch_private(byte);
		return;
	}

	if (intermediate)
		return;

	switch (byte) {
	case 0x37: // 7 - DECSC - Save Cursor
		// TODO : save character set!
		saved_cursor = cursor;
		break;
	case 0x38: // 8 - DECRC - Restore Cursor
		// TODO : restore character set!
		cursor = saved_cursor;
		break;
	case 0x3D: // = - DECKPAM - Keypad Application Mode
		mode[DECKPAM] = true;
		break;
	case 0x3E: // > - DECKPNM - Keypad Numeric Mode
		mode[DECKPAM] = false;
		break;
	case 0x44: // D - IND - Index
		newline();
		break;
	case 0x45: // E - NEL - Next Line
		cursor.x = 0;
		newline();
		break;
	case 0x48: // H - HTS - Horizontal Tabulation Set
		tabstops[cursor.x] = true;
		break;
	case 0x4D: // M - RI - Reverse Index
		if (cursor.y > scroll_top) warpto(cursor.x, cursor.y - 1);
		else scrolldown();
		cursor.last_column = false;
		break;
	case 0x5A: // Z - DECID - Identify Terminal
		write_ptmx(DEVICE_ATTRS, sizeof(DEVICE_ATTRS));
		break;
	case 0x63: // c - RIS - Reset To Initial Set
		vtreset();
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
	case 0x38: // 8 - DECALN - Screen Alignment Display
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
	NEXT(GROUND);

	if (intermediate == 0x3F) {
		csi_dispatch_private(byte);
		return;
	}

	if (intermediate)
		return;

	if (parameter_index == MAX_PARAMETERS)
		parameter_index = MAX_PARAMETERS - 1;

	switch (byte) {
	case 0x41: // A - CUU - Cursor Up
	case 0x42: // B - CUD - Cursor Down
	case 0x43: // C - CUF - Cursor Forward
	case 0x44: // D - CUB - Cursor Backward
		if (!parameters[0]) parameters[0] = 1;

		switch (byte) {
		case 0x41: warpto(cursor.x, cursor.y - parameters[0]); break;
		case 0x42: warpto(cursor.x, cursor.y + parameters[0]); break;
		case 0x43: warpto(cursor.x + parameters[0], cursor.y); break;
		case 0x44: warpto(cursor.x - parameters[0], cursor.y); break;
		}

		break;
	case 0x48: // H - CUP - Cursor Position
	case 0x66: // f - HVP - Horizontal and Vertical Position
		warpto(parameters[1] - 1, parameters[0] - 1 + (mode[DECOM] ? scroll_top : 0));
		break;
	case 0x4A: // J - ED - Erase In Display
		erase_display();
		break;
	case 0x4B: // K - EL - Erase In Line
		erase_line();
		break;
	case 0x50: // P - DCH - Delete Character
		delete_character();
		break;
	case 0x63: // c - DA - Device Attributes
		if (parameters[0] == 0)
			write_ptmx(DEVICE_ATTRS, sizeof(DEVICE_ATTRS));
		break;
	case 0x67: // g - TBC - Tabulation Clear
		if (!parameters[0])
			tabstops[cursor.x] = false;
		else if (parameters[0] == 3)
			memset(tabstops, 0, screen_width * sizeof(bool));
		break;
	case 0x68: // h - SM - Set Mode
		set_mode(true);
		break;
	case 0x6C: // l - RM - Reset Mode
		set_mode(false);
		break;
	case 0x6D: // m - SGR - Select Graphic Rendition
		select_graphic_rendition();
		break;
	case 0x6E: // n - DSR - Device Status Report
		device_status_report();
		break;
	case 0x72: // r - DECSTBM - Set Top and Bottom Margins
		if (!parameters[0]) parameters[0] = 1;
		if (!parameters[1] || parameters[1] > screen_height)
			parameters[1] = screen_height;

		if (parameters[0] < parameters[1]) {
			scroll_top = parameters[0] - 1;
			scroll_bottom = parameters[1] - 1;
			warpto(0, mode[DECOM] ? scroll_top : 0);
		}
		break;
	default:
		warnx("unrecognized CSI: %c/%x", byte, byte);
		break;
	}
}

static void
csi_dispatch_private(unsigned char byte)
{
	switch (byte) {
	case 0x68: // h - SM - Set Mode
		set_mode(true);
		break;
	case 0x6C: // l - RM - Reset Mode
		set_mode(false);
		break;
	}
}

static void
erase_display()
{
	int i, n;

	switch (parameters[0]) {
	case 0:
		i = cursor.x + cursor.y * screen_width;
		n = screen_width * screen_height;
		break;
	case 1:
		i = 0;
		n = cursor.x + cursor.y * screen_width + 1;
		break;
	case 2:
		i = 0;
		n = screen_width * screen_height;
		break;
	}

	for (; i < n; i++)
		screen[i] = cursor.attrs;

	cursor.last_column = false;
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
		screen[x + cursor.y * screen_width] = cursor.attrs;

	cursor.last_column = false;
}

static void
delete_character()
{
	int max;

	if (parameters[0] == 0)
		parameters[0] = 1;

	if (parameters[0] > (max = screen_width - cursor.x - 1))
		parameters[0] = max;

	memmove(&screen[cursor.x + cursor.y * screen_width],
		&screen[cursor.x + cursor.y * screen_width + parameters[0]],
		(screen_width - parameters[0]) * sizeof(struct cell));

	memset(&screen[(cursor.y + 1) * screen_width - parameters[0]], 0,
		parameters[0] * sizeof(struct cell));

	cursor.last_column = false;
}

static void
set_mode(bool value)
{
	int i;

	for (i = 0; i <= parameter_index; i++)
		if (!intermediate) {
			if (parameters[i] == 20)
				mode[LNM] = value;
			else
				warnx("set mode %i=%i", parameters[0], value);
		} else if (intermediate == 0x3F) {
			switch (parameters[i]) {
			case 1: mode[DECCKM] = value; break;
			// case 2: mode[DECANM] = value; break;
			case 3:
				if ((mode[DECCOLM] = value))
					vtresize(132, screen_height);
				else
					vtresize(80, screen_height);
				break;
			// case 4: mode[DECSCLM] = value; break;
			case 5: mode[DECSCNM] = value; break;
			case 6:
				warpto(0, (mode[DECOM] = value) ? scroll_top : 0);
				break;
			case 7: mode[DECAWM] = value; break;
			case 8: mode[DECARM] = value; break;
			// case 9: mode[DECINLM] = value; break;
			case 25: mode[DECTCEM] = value; break;
			default:
				warnx("set mode ?%i=%i", parameters[0], value);
				break;
			}
		}
}

static void
select_graphic_rendition()
{
	struct cell attrs;
	int i;

	attrs = cursor.attrs;

	for (i = 0; i <= parameter_index; i++)
		switch (parameters[i]) {
		case 0:
			memset(&attrs, 0, sizeof(attrs));
			cursor.conceal = false;
			break;
		case 1: attrs.intensity = INTENSITY_BOLD; break;
		case 2: attrs.intensity = INTENSITY_FAINT; break;
		case 3: attrs.italic = true; break;
		case 4: attrs.underline = UNDERLINE_SINGLE; break;
		case 5: attrs.blink = BLINK_SLOW; break;
		case 6: attrs.blink = BLINK_FAST; break;
		case 7: attrs.negative = true; break;
		case 8: cursor.conceal = true; break;
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
		case 28: cursor.conceal = false; break;
		case 29: attrs.crossed_out = false; break;
		case 38: case 48: // ignore next few arguments
			if (i++ == parameter_index) return;
			if (parameters[i] == 5) i++;
			else if (parameters[i] == 2) i += 3;
			break;
		case 51: attrs.frame = FRAME_FRAMED; break;
		case 52: attrs.frame = FRAME_ENCIRCLED; break;
		case 53: attrs.overline = true; break;
		case 54: attrs.frame = FRAME_NONE; break;
		case 55: attrs.overline = false; break;
		}

	cursor.attrs = attrs;
}

static void
device_status_report()
{
	// VT100 Ready, No malfunctions detected
	static const unsigned char DEVICE_STATUS[] = { 0x1B, 0x5B, 0x30, 0x6E };

	// Cursor Position Report
	static const unsigned char CPR_START[] = { 0x1B, 0x5B };
	static const unsigned char CPR_MIDDLE[] = { 0x3B };
	static const unsigned char CPR_END[] = { 0x52 };

	if (parameters[0] == 5) {
		write_ptmx(DEVICE_STATUS, sizeof(DEVICE_STATUS));
	} else if (parameters[0] == 6) {
		write_ptmx(CPR_START, sizeof(CPR_START));
		write_ptmx_num((mode[DECOM] ? cursor.y - scroll_top : cursor.y) + 1);
		write_ptmx(CPR_MIDDLE, sizeof(CPR_MIDDLE));
		write_ptmx_num(cursor.x + 1);
		write_ptmx(CPR_END, sizeof(CPR_END));
	}
}