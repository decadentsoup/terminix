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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terminix.h"

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

enum state52 {
	STATE52_GROUND,
	STATE52_ESCAPE,
	STATE52_DCA1,
	STATE52_DCA2
};

static enum state state;
static unsigned char intermediate;
static unsigned short parameters[MAX_PARAMETERS];
static unsigned char parameter_index;
static char osc[512];
static size_t osc_size, osc_data_offset;

static enum state52 state52;

// VT100 with Processor Option, Advanced Video Option, and Graphics Option
static const char DEVICE_ATTRS[] = "\x1B\x5B\x3F\x31\x3B\x37\x63";

static void interpret(unsigned char);
static void interpret52(unsigned char);
static void print(unsigned char);
static void execute(unsigned char);
static void collect(unsigned char);
static void param(unsigned char);
static void esc_dispatch(unsigned char);
static void esc_dispatch_private(unsigned char);
static void esc_dispatch_scs(unsigned char);
static void unrecognized_escape(unsigned char);
static void csi_dispatch(unsigned char);
static void csi_dispatch_private(unsigned char);
static void move_cursor(unsigned char, int);
static void erase_display(int);
static void erase_line(int);
static void delete_character(void);
static void device_status_report(void);
static void configure_leds(void);
static void set_mode(bool);
static void select_graphic_rendition(void);
static void osc_start(void);
static void osc_put(unsigned char);
static void osc_end(void);
static void change_colors(const char *);
static void change_color(int, const char *);
static void parse_rgbhex(struct color *, const char *);
static void parse_rgb(struct color *, const char *);
static uint8_t parse_rgb_part(const char *);
static void parse_rgbi(struct color *, const char *);

void
vtinterp(const unsigned char *buffer, size_t bufsize)
{
	size_t i;

	for (i = 0; i < bufsize; i++)
		if (mode[DECANM])
			interpret(buffer[i]);
		else
			interpret52(buffer[i]);
}

#define COND(condition, action) \
	do { if (condition) { action; return; } } while(0);

#define NEXT(target) (state = STATE_##target)

static void
interpret(unsigned char byte)
{
	// substitute and cancel controls
	// TODO : should this execute an OSC instruction?
	if (byte == 0x18 || byte == 0x1A) {
		NEXT(GROUND);
		putch(0xFFFD);
		return;
	}

	// escape control
	if (byte == 0x1B) {
		if (state == STATE_OSC_STRING) osc_end();
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
		COND(byte == 0x5D, osc_start(); NEXT(OSC_STRING));
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
		COND(byte == 0x07, osc_end(); NEXT(GROUND));
		COND(byte >= 0x20, osc_put(byte));
		break;
	case STATE_SOS_STRING:
	case STATE_PM_STRING:
	case STATE_APC_STRING:
		// ignore everything
		break;
	}
}

static void
interpret52(unsigned char byte)
{
	if (byte == 0x1B) {
		state52 = STATE52_ESCAPE;
	} else if (byte <= 0x1F) {
		execute(byte);
	} else if (state52 == STATE52_DCA1) {
		intermediate = byte;
		state52 = STATE52_DCA2;
	} else if (state52 == STATE52_DCA2) {
		warpto(byte - 0x20, intermediate - 0x20);
		state52 = STATE52_GROUND;
	} else if (state52 == STATE52_ESCAPE) {
		state52 = STATE52_GROUND;

		switch (byte) {
		case 'A': case 'B': case 'C': case 'D':
			move_cursor(byte, 1);
			break;
		case 'F':
			warnx("vt52 - select special graphics character set");
			break;
		case 'G':
			warnx("vt52 - select ascii character set");
			break;
		case 'H':
			cursor.x = 0;
			cursor.y = 0;
			break;
		case 'I':
			revline();
			break;
		case 'J':
			erase_display(0);
			break;
		case 'K':
			erase_line(0);
			break;
		case 'Y':
			state52 = STATE52_DCA1;
			break;
		case 'Z':
			ptwrite("\33/Z");
			break;
		case '=':
			mode[DECKPAM] = true;
			break;
		case '>':
			mode[DECKPAM] = false;
			break;
		case '<':
			mode[DECANM] = true;
			break;
		default:
			intermediate = 0;
			unrecognized_escape(byte);
			break;
		}
	} else {
		print(byte);
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
		ring_bell();
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
		mode[SHIFT_OUT] = true;
		break;
	case 0x0F: // Shift In
		mode[SHIFT_OUT] = false;
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

	if (intermediate == 0x28 || intermediate == 0x29) {
		esc_dispatch_scs(byte);
		return;
	}

	if (intermediate) {
		unrecognized_escape(byte);
		return;
	}

	switch (byte) {
	case 0x37: // 7 - DECSC - Save Cursor
		saved_cursor = cursor;
		break;
	case 0x38: // 8 - DECRC - Restore Cursor
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
	case 0x46: // F - XTerm*hpLowerleftBugCompat
		cursor.x = 0;
		cursor.y = scroll_bottom;
		break;
	case 0x48: // H - HTS - Horizontal Tabulation Set
		tabstops[cursor.x] = true;
		break;
	case 0x4D: // M - RI - Reverse Index
		revline();
		break;
	case 0x5A: // Z - DECID - Identify Terminal
		ptwrite("%s", DEVICE_ATTRS);
		break;
	case 0x5C: // \ - ST - String Terminator
		break; // nothing to do
	case 0x63: // c - RIS - Reset To Initial Set
		reset();
		break;
	default:
		unrecognized_escape(byte);
		break;
	}
}

static void
esc_dispatch_private(unsigned char byte)
{
	int x, y;

	switch (byte) {
	case 0x33: // 3 - DECDHL - Double-Height Line (Top)
		lines[cursor.y]->dimensions = DOUBLE_HEIGHT_TOP;
		break;
	case 0x34: // 4 - DECDHL - Double-Height Line (Bottom)
		lines[cursor.y]->dimensions = DOUBLE_HEIGHT_BOTTOM;
		break;
	case 0x35: // 5 - DECSWL - Single-Width Line
		lines[cursor.y]->dimensions = SINGLE_WIDTH;
		break;
	case 0x36: // 6 - DECDWL - Double-Width Line
		lines[cursor.y]->dimensions = DOUBLE_WIDTH;
		break;
	case 0x38: // 8 - DECALN - Screen Alignment Display
		for (y = 0; y < screen_height; y++)
			for (x = 0; x < screen_width; x++)
				lines[y]->cells[x].code_point = 0x45;
		break;
	default:
		unrecognized_escape(byte);
		break;
	}
}

static void
esc_dispatch_scs(unsigned char byte)
{
	const uint32_t *charset;

	switch (byte) {
	case 0x30: charset = charset_dec_graphics; break;
	case 0x31: charset = NULL; break; // Alternate ROM Standard
	case 0x32: charset = NULL; break; // Alternate ROM Graphics
	case 0x41: charset = charset_united_kingdom; break;
	case 0x42: charset = NULL; break; // ASCII
	default: charset = NULL; break;
	}

	cursor.charset[intermediate == 0x28 ? 0 : 1] = charset;
}

static void
unrecognized_escape(unsigned char byte)
{
	if (isprint(byte))
		warnx("unrecognized escape \"%c%c\"", intermediate, byte);
	else
		warnx("unrecognized escape \"%c\" 0x%X", intermediate, byte);
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
		move_cursor(byte, parameters[0] ? parameters[0] : 1);
		break;
	case 0x48: // H - CUP - Cursor Position
	case 0x66: // f - HVP - Horizontal and Vertical Position
		warpto(parameters[1] - 1, parameters[0] - 1 + (mode[DECOM] ? scroll_top : 0));
		break;
	case 0x4A: // J - ED - Erase In Display
		erase_display(parameters[0]);
		break;
	case 0x4B: // K - EL - Erase In Line
		erase_line(parameters[0]);
		break;
	case 0x50: // P - DCH - Delete Character
		delete_character();
		break;
	case 0x63: // c - DA - Device Attributes
		if (parameters[0] == 0)
			ptwrite("%s", DEVICE_ATTRS);
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
	case 0x71: // q - DECLL - Load LEDs
		configure_leds();
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
move_cursor(unsigned char direction, int amount)
{
	switch (direction) {
	case 0x41: warpto(cursor.x, cursor.y - amount); break;
	case 0x42: warpto(cursor.x, cursor.y + amount); break;
	case 0x43: warpto(cursor.x + amount, cursor.y); break;
	case 0x44: warpto(cursor.x - amount, cursor.y); break;
	}
}

static void
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

static void
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

static void
delete_character()
{
	struct line *line;
	int max;

	line = lines[cursor.y];

	if (parameters[0] == 0)
		parameters[0] = 1;

	if (parameters[0] > (max = screen_width - cursor.x - 1))
		parameters[0] = max;

	memmove(&line->cells[cursor.x], &line->cells[cursor.x + parameters[0]],
		(screen_width - parameters[0] - cursor.x) * sizeof(struct cell));

	memset(&line->cells[screen_width - parameters[0]], 0,
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
			case 2: mode[DECANM] = value; break;
			case 3: resize(value ? 132 : 80, screen_height); break;
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
	if (parameters[0] == 5)
		// VT100 Ready, No malfunctions detected
		ptwrite("\x1B\x5B\x30\x6E");
	else if (parameters[0] == 6)
		// Cursor Position Report
		ptwrite("\x1B\x5B%d\x3B%d\x52",
			(mode[DECOM] ? cursor.y - scroll_top : cursor.y) + 1,
			cursor.x + 1);
}

static void
configure_leds()
{
	int i;

	for (i = 0; i <= parameter_index; i++)
		switch (parameters[i]) {
		case 0: warnx("TODO : Clear LEDs"); break;
		case 1: warnx("TODO : LED 1 On"); break;
		case 2: warnx("TODO : LED 2 On"); break;
		case 3: warnx("TODO : LED 3 On"); break;
		case 4: warnx("TODO : LED 4 On"); break;
		}
}

#define OSC_IS(prefix) (!strncmp(prefix ";", osc, sizeof(prefix)))

static void
osc_start()
{
	memset(osc, 0, sizeof(osc));
	osc_size = 0;
	osc_data_offset = 0;
}

static void
osc_put(unsigned char byte)
{
	if (osc_size < sizeof(osc) - 2) {
		osc[osc_size++] = byte;

		if (!osc_data_offset && byte == ';')
			osc_data_offset = osc_size;
	}
}

static void
osc_end()
{
	const char *data;

	data = &osc[osc_data_offset];

	if (OSC_IS("0")) {
		set_window_title(data);
		set_icon_name(data);
	} else if (OSC_IS("1") || OSC_IS("2L")) {
		set_icon_name(data);
	} else if (OSC_IS("2") || OSC_IS("21")) {
		set_window_title(data);
	} else if (OSC_IS("3")) {
		warnx("TODO : set X property to %s", data);
	} else if (OSC_IS("4")) {
		change_colors(data);
	}
}

static void
change_colors(const char *data)
{
	char name[100];
	int index, offset;

	while (sscanf(data, "%d;%99[^;]%n", &index, name, &offset) == 2 ||
		sscanf(data, ";%d;%99[^;]%n", &index, name, &offset) == 2) {
		change_color(index, name);
		data += offset;
	}
}

#define COLOR_IS(prefix) (!strncasecmp(prefix ":", name, sizeof(prefix)))

static void
change_color(int index, const char *name)
{
	if (index < 0 || index > 255) {
		warnx("Color index %i out of range (0..255)", index);
		return;
	}

	if (name[0] == '#')
		parse_rgbhex(&palette[index], name);
	else if (COLOR_IS("rgb"))
		parse_rgb(&palette[index], name);
	else if (COLOR_IS("rgbi"))
		parse_rgbi(&palette[index], name);
	else if (COLOR_IS("ciexyz"))
		warnx("TODO : CIEXYZ Specification: %s", name);
	else if (COLOR_IS("cieuvy"))
		warnx("TODO : CIEuvY Specification: %s", name);
	else if (COLOR_IS("ciexyy"))
		warnx("TODO : CIExyY Specification: %s", name);
	else if (COLOR_IS("cielab"))
		warnx("TODO : CIELab Specification: %s", name);
	else if (COLOR_IS("tekhvc"))
		warnx("TODO : TekHVC Specification: %s", name);
	else
		warnx("TODO : Unknown Color (Named?): %s", name);
}

static void
parse_rgbhex(struct color *colorp, const char *text)
{
	long long hex;

	hex = strtoll(&text[1], NULL, 16);

	switch (strlen(text) - 1) {
	case 3:
		colorp->r = (hex >> 8) * 0x10;
		colorp->g = (hex >> 4 & 0xF) * 0x10;
		colorp->b = (hex & 0xF) * 0x10;
		break;
	case 6:
		colorp->r = (hex >> 16);
		colorp->g = (hex >> 8 & 0xFF);
		colorp->b = (hex & 0xFF);
		break;
	case 9:
		colorp->r = (hex >> 28);
		colorp->g = (hex >> 16 & 0xFF);
		colorp->b = (hex >> 4 & 0xFF);
		break;
	case 12:
		colorp->r = (hex >> 40);
		colorp->g = (hex >> 24 & 0xFF);
		colorp->b = (hex >> 8 & 0xFF);
		break;
	default:
		warnx("Invalid color specification: %s", text);
		break;
	}
}

static void
parse_rgb(struct color *colorp, const char *text)
{
	char r[5], g[5], b[5];

	if (sscanf(&text[4], "%4[^/]/%4[^/]/%4[^/]", r, g, b) != 3) {
		warnx("Invalid color specification: %s", text);
		return;
	}

	colorp->r = parse_rgb_part(r);
	colorp->g = parse_rgb_part(g);
	colorp->b = parse_rgb_part(b);
}

static uint8_t
parse_rgb_part(const char *text)
{
	long hex;

	hex = strtol(text, NULL, 16);

	switch (strlen(text)) {
	case 1: return hex * 0x10;
	case 2: return hex;
	case 3: return hex / 0x10;
	case 4: return hex / 0x100;
	}

	warnx("Impossible condition met in parse_rgb_part()!");
	return 0;
}

static void
parse_rgbi(struct color *colorp, const char *text)
{
	float r, g, b;

	if (sscanf(&text[5], "%f/%f/%f", &r, &g, &b) != 3 ||
		r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1) {
		warnx("Invalid color specification: %s", text);
		return;
	}

	colorp->r = r * 255;
	colorp->g = g * 255;
	colorp->b = b * 255;
}