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
#include <string.h>
#include "ptmx.h"
#include "screen.h"
#include "vtinterp.h"

#define MAX_PARAMETERS 16
#define PARAMETER_MAX 16383

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

static enum state state;
static unsigned char intermediate;
static unsigned short parameters[MAX_PARAMETERS];
static unsigned char parameter_index;

// VT100 with Processor Option, Advanced Video Option, and Graphics Option
static const unsigned char DEVICE_ATTRS[] =
	{ 0x1B, 0x5B, 0x3F, 0x31, 0x3B, 0x37, 0x63 };

static void interpret(unsigned char);
static void print(unsigned char);
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
		putch(0xFFFD);
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
		// COND(byte <= 0x7E, print(byte));
		print(byte);
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
print(unsigned char byte)
{
	static char sequence_size, sequence_index;
	static long code_point;

	switch (sequence_size) {
	case 0:
		sequence_index = 0;
		code_point = 0;

		if (!(byte & 0x80)) {
			putch(byte);
		} else if ((byte & 0xE0) == 0xC0) {
			sequence_size = 2;
			code_point = (byte & ~0xE0) << 6;
		} else if ((byte & 0xF0) == 0xE0) {
			sequence_size = 3;
			code_point = (byte & ~0xF0) << 12;
		} else if ((byte & 0xF8) == 0xF0) {
			sequence_size = 4;
			code_point = (byte & ~0xF8) << 18;
		} else {
			sequence_size = 0;
			putch(0xFFFD);
		}

		break;
	case 2:
		putch(code_point | (byte & ~0xC0));
		sequence_size = 0;
		break;
	case 3:
		switch (sequence_index++) {
		case 0: code_point |= (byte & ~0xC0) << 6; break;
		default:
			putch(code_point | (byte & ~0xC0));
			sequence_size = 0;
			break;
		}
		break;
	case 4:
		switch (sequence_index++) {
		case 0: code_point |= (byte & ~0xC0) << 12; break;
		case 1: code_point |= (byte & ~0xC0) << 6; break;
		default:
			putch(code_point | (byte & ~0xC0));
			sequence_size = 0;
			break;
		}
		break;
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
		newline();
		if (mode[LNM]) cursor.x = 0;
		break;
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
		reset();
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
	case 0x33: // 3 - DECDHL - Double-Height Line (Top)
		lines[cursor.y].dimensions = DOUBLE_HEIGHT_TOP;
		break;
	case 0x34: // 4 - DECDHL - Double-Height Line (Bottom)
		lines[cursor.y].dimensions = DOUBLE_HEIGHT_BOTTOM;
		break;
	case 0x35: // 5 - DECSWL - Single-Width Line
		lines[cursor.y].dimensions = SINGLE_WIDTH;
		break;
	case 0x36: // 6 - DECDWL - Double-Width Line
		lines[cursor.y].dimensions = DOUBLE_WIDTH;
		break;
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
	default:
		return;
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
	default: return;
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
				warnx("set mode %i=%i", parameters[i], value);
		} else if (intermediate == 0x3F) {
			switch (parameters[i]) {
			case 1: mode[DECCKM] = value; break;
			// case 2: mode[DECANM] = value; break;
			case 3:
				if ((mode[DECCOLM] = value))
					resize(132, screen_height);
				else
					resize(80, screen_height);
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
				warnx("set mode ?%i=%i", parameters[i], value);
				break;
			}
		}
}

static void
select_graphic_rendition()
{
	struct cell attrs;
	int i, parameter;

	attrs = cursor.attrs;

	for (i = 0; i <= parameter_index; i++) {
		parameter = parameters[i];

		if (parameter >= 10 && parameter <= 19) {
			attrs.font = parameter - 10;
		} else if (parameter >= 30 && parameter <= 37) {
			attrs.foreground.r = parameter - 30;
			attrs.fg_truecolor = false;
		} else if (parameter >= 40 && parameter <= 47) {
			attrs.background.r = parameter - 40;
			attrs.bg_truecolor = false;
		} else if (parameter >= 90 && parameter <= 97) {
			attrs.foreground.r = parameter - 90 + 8;
			attrs.fg_truecolor = false;
		} else if (parameter >= 100 && parameter <= 107) {
			attrs.background.r = parameter - 100 + 8;
			attrs.bg_truecolor = false;
		} else {
			switch (parameter) {
			case 0:
				attrs = default_attrs;
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
			case 20: attrs.fraktur = true; break;
			case 21: attrs.underline = UNDERLINE_DOUBLE; break;
			case 22: attrs.intensity = INTENSITY_NORMAL; break;
			case 23:
				attrs.italic = false;
				attrs.fraktur = false;
				break;
			case 24: attrs.underline = UNDERLINE_NONE; break;
			case 25: attrs.blink = BLINK_NONE; break;
			case 27: attrs.negative = false; break;
			case 28: cursor.conceal = false; break;
			case 29: attrs.crossed_out = false; break;
			case 38: case 48:
				if (i++ == parameter_index) return;

				switch (parameters[i++]) {
				case 2:
					if (parameter == 38) {
						attrs.foreground.r = parameters[i++];
						attrs.foreground.g = parameters[i++];
						attrs.foreground.b = parameters[i];
						attrs.fg_truecolor = true;
					} else {
						attrs.background.r = parameters[i++];
						attrs.background.g = parameters[i++];
						attrs.background.b = parameters[i];
						attrs.bg_truecolor = true;
					}
					break;
				case 5:
					if (parameter == 38) {
						attrs.foreground.r = parameters[i];
						attrs.fg_truecolor = false;
					} else {
						attrs.background.r = parameters[i];
						attrs.fg_truecolor = false;
					}

					break;
				}

				break;
			case 51: attrs.frame = FRAME_FRAMED; break;
			case 52: attrs.frame = FRAME_ENCIRCLED; break;
			case 53: attrs.overline = true; break;
			case 54: attrs.frame = FRAME_NONE; break;
			case 55: attrs.overline = false; break;
			}
		}
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