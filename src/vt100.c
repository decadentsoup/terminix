// vt100.c - ECMA-48/ANSI X3.64 emulator routines
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

static enum state state;
static unsigned char intermediates[2];
static unsigned short parameters[MAX_PARAMETERS];
static unsigned char parameter_index;
static char osc[512];
static size_t osc_size, osc_data_offset;

// VT100 with Processor Option, Advanced Video Option, and Graphics Option
static const char DEVICE_ATTRS[] = "\x1B\x5B\x3F\x31\x3B\x37\x63";

static void collect(unsigned char);
static void param(unsigned char);
static void esc_dispatch(unsigned char);
static const uint32_t *get_charset_94(unsigned char, unsigned char);
static const uint32_t *get_charset_96(unsigned char);
static void csi_dispatch(unsigned char);
static void csi_dispatch_private(unsigned char);
static void delete_character(void);
static void device_status_report(void);
static void configure_leds(void);
static void set_ansi_mode(bool);
static void set_dec_mode(bool);
static void select_graphic_rendition(void);
static void osc_start(void);
static void osc_put(unsigned char);
static void osc_end(void);
static void change_colors(const char *);
static void change_color(int, const char *);

#define COND(condition, action) \
	do { if (condition) { action; return; } } while(0);

#define NEXT(target) (state = STATE_##target)

void
vt100(unsigned char byte)
{
	// TODO : https://www.cl.cam.ac.uk/~mgk25/unicode.html#term
	// Should we process UTF-8 data before passing it to this state machine?

	// TODO : cleanup to the way OSC strings are handled to be more
	// compliant with the behavior of DEC terminals

	// TODO : support for 8-bit controls when enabled
	// NOTE : 8-bit and UTF-8 cannot work at the same time

	// substitute and cancel controls
	// TODO : VT520 does not print 0xFFFD for CAN, only SUB
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
		print(byte);
		break;
	case STATE_ESCAPE:
		memset(intermediates, 0, sizeof(intermediates));
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
	case STATE_DCS_PARAM:
	case STATE_DCS_INTERMEDIATE:
	case STATE_DCS_PASSTHROUGH:
	case STATE_DCS_IGNORE:
		warnx("TODO : Device Control Strings");
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
collect(unsigned char byte)
{
	if (!intermediates[0])
		intermediates[0] = byte;
	else if (!intermediates[1])
		intermediates[1] = byte;
	else
		intermediates[0] = 255;
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

// [1] XTerm*hpLowerleftBugCompat
// [2] DECREPTPARM/DECREQTPARM
static void
esc_dispatch(unsigned char byte)
{
	NEXT(GROUND);

	switch (intermediates[0]) {
	case 0:
		if (intermediates[1]) break;
		switch (byte) {
		/*DECBI  */ case '6': warnx("TODO : Back Index"); return;
		/*DECSC  */ case '7': save_cursor(); return;
		/*DECRC  */ case '8': restore_cursor(); return;
		/*DECFI  */ case '9': warnx("TODO : Forward Index"); return;
		/*DECANM */ case '<': /* nothing to do */ return;
		/*DECKPAM*/ case '=': setmode(DECKPAM, true); return;
		/*DECKPNM*/ case '>': setmode(DECKPAM, false); return;
		/*IND    */ case 'D': newline(); return;
		/*NEL    */ case 'E': nextline(); return;
		/*[1]    */ case 'F': warpto(0, scroll_bottom); return;
		/*HTS    */ case 'H': settab(); return;
		/*SCS    */ case 'I': warnx("TODO : Designate Character Set"); return;
		/*RI     */ case 'M': revline(); return;
		/*SS2    */ case 'N': singleshift(G2); return;
		/*SS3    */ case 'O': singleshift(G3); return;
		/*SCODFK */ case 'Q': warnx("TODO : SCO Define Function Key"); return;
		/*DECID  */ case 'Z': ptwrite("%s", DEVICE_ATTRS); return;
		/*ST     */ case'\\': /* nothing to do */ return;
		/*RIS    */ case 'c': reset(); return;
		/*LS2    */ case 'n': lockingshift(GL, G2); return;
		/*LS3    */ case 'o': lockingshift(GL, G3); return;
		/*[2]    */ case 'x': warnx("TODO : implement DECRE(P/Q)TPARM"); return;
		/*DECTST */ case 'y': warnx("TODO : implement DECTST"); return;
		/*LS3R   */ case '|': lockingshift(GR, G3); return;
		/*LS2R   */ case '}': lockingshift(GR, G2); return;
		/*LS1R   */ case '~': lockingshift(GR, G1); return;
		}
		break;
	case ' ':
		if (intermediates[1]) break;
		switch (byte) {
		/*S7C1T*/ case 'F': setmode(S8C1T, false); return;
		/*S8C1T*/ case 'G': setmode(S8C1T, true); return;
		}
		break;
	case '#':
		if (intermediates[1]) break;
		switch (byte) {
		/*DECDHL*/ case '3': setlinea(DOUBLE_HEIGHT_TOP); return;
		/*DECDHL*/ case '4': setlinea(DOUBLE_HEIGHT_BOTTOM); return;
		/*DECSWL*/ case '5': setlinea(SINGLE_WIDTH); return;
		/*DECDWL*/ case '6': setlinea(DOUBLE_WIDTH); return;
		/*DECALN*/ case '8': screen_align(); return;
		}
		break;
	case '%':
		switch (intermediates[1]) {
		case 0:
			switch (byte) {
			case '@': warnx("TODO : deactivate UTF-8 if possible"); return;
			case 'G': warnx("TODO : activate UTF-8 reversibly"); return;
			}
			break;
		case '/':
			switch (byte) {
			case 'G': warnx("TODO : activate UTF-8 level 1 irreversibly"); return;
			case 'H': warnx("TODO : activate UTF-8 level 2 irreversibly"); return;
			case 'I': warnx("TODO : activate UTF-8 level 3 irreversibly"); return;
			}
			break;
		}
		break;
	case '(':
		setcharset(G0, get_charset_94(intermediates[1], byte));
		return;
	case ')':
		setcharset(G1, get_charset_94(intermediates[1], byte));
		return;
	case '*':
		setcharset(G2, get_charset_94(intermediates[1], byte));
		return;
	case '+':
		setcharset(G3, get_charset_94(intermediates[1], byte));
		return;
	case '-':
		setcharset(G1, get_charset_96(byte));
		return;
	case '.':
		setcharset(G2, get_charset_96(byte));
		return;
	case '/':
		setcharset(G3, get_charset_96(byte));
		return;
	case 255:
		warnx("too many intermediates in escape sequence");
		return;
	}

	unrecognized_escape(intermediates[0], intermediates[1], byte);
}

static const uint32_t *
get_charset_94(unsigned char c1, unsigned char c2)
{
	switch (c1) {
	case 0:
		switch (c2) {
		case '0': return charset_dec_graphics;
		case '1': warnx("TODO : DEC Alternate Character ROM Standard Characters"); return NULL;
		case '2': warnx("TODO : DEC Alternate Character ROM Special Characters"); return NULL;
		// case '5': return finnish;
		// case '6': return norweigian_danish;
		// case '7': return swedish;
		// case '9': return french_canadian;
		case 'A': return charset_united_kingdom;
		case 'B': return NULL; // ASCII
		// case 'C': return finnish;
		// case '>': return dec_technical_character_set;
		// case 'E': return norweigian_danish;
		// case 'H': return swedish;
		// case 'K': return german;
		// case '`': return norweigian_danish;
		// case 'Q': return french_canadian;
		// case 'R': return french;
		// case '=': return swiss;
		// case '<': return user_preferred_supplemental_set;
		// case 'Y': return italian;
		// case 'Z': return spanish;
		}
		break;
	// case '"':
	// 	switch (c2) {
	// 	case '4': return dec_hebrew;
	// 	case '>': return greek;
	// 	case '?': return dec_greek;
	// 	}
	// 	break;
	// case '%':
	// 	switch (c2) {
	// 	case '0': return dec_turkish;
	// 	case '2': return turkish;
	// 	case '3': return scs;
	// 	case '5': return dec_supplemental;
	// 	case '6': return portuguese;
	// 	case '=': return hebrew;
	// 	}
	// 	break;
	// case '&':
	// 	switch (c2) {
	// 	case '4': return dec_cyrillic;
	// 	case '5': return russian;
	// 	}
	// 	break;
	}

	warnx("Unrecognized 94-character set: '%c%c'", c1, c2);
	return NULL; // TODO : should we do a no-op instead?
}

static const uint32_t *
get_charset_96(unsigned char c)
{
	// switch (c) {
	// case 'A': return iso_latin1_supplemental;
	// case 'B': return iso_latin2_supplemental;
	// case 'F': return iso_greek_supplemental;
	// case 'H': return iso_hebrew_supplemental;
	// case 'L': return iso_latin_cyrillic;
	// case 'M': return iso_latin5_supplemental;
	// case '<': return user_preferred_supplemental;
	// }

	warnx("Unrecognized 96-character set: '%c'", c);
	return NULL; // TODO : should we do a no-op instead?
}

static void
csi_dispatch(unsigned char byte)
{
	NEXT(GROUND);

	if (intermediates[0] == 255 || intermediates[1]) {
		warnx("too many intermediates in CSI sequence");
		return;
	}

	if (intermediates[0] == 0x3F) {
		csi_dispatch_private(byte);
		return;
	}

	if (intermediates[0])
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
		warpto(parameters[1] - 1, parameters[0] - 1 + (getmode(DECOM) ? scroll_top : 0));
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
		set_ansi_mode(true);
		break;
	case 0x6C: // l - RM - Reset Mode
		set_ansi_mode(false);
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
			warpto(0, getmode(DECOM) ? scroll_top : 0);
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
		set_dec_mode(true);
		break;
	case 0x6C: // l - RM - Reset Mode
		set_dec_mode(false);
		break;
	}
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
set_ansi_mode(bool value)
{
	int i;

	for (i = 0; i <= parameter_index; i++)
		if (parameters[i] == 20)
			setmode(LNM, value);
		else
			warnx("set mode %i=%i", parameters[i], value);
}

static void
set_dec_mode(bool value)
{
	int i;

	for (i = 0; i <= parameter_index; i++)
		switch (parameters[i]) {
		case 1: setmode(DECCKM, value); break;
		case 2: setmode(DECANM, value); break;
		case 3: resize(value ? 132 : 80, screen_height); break;
		case 4: setmode(DECSCLM, value); break;
		case 5: setmode(DECSCNM, value); break;
		case 6:
			warpto(0, setmode(DECOM, value) ? scroll_top : 0);
			break;
		case 7: setmode(DECAWM, value); break;
		case 8: setmode(DECARM, value); break;
		case 9: setmode(DECINLM, value); break;
		case 25: setmode(DECTCEM, value); break;
		default:
			warnx("set mode ?%i=%i", parameters[i], value);
			break;
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
						attrs.bg_truecolor = false;
					}

					break;
				}

				break;
			case 39:
				attrs.foreground = default_attrs.foreground;
				attrs.fg_truecolor = default_attrs.fg_truecolor;
				break;
			case 49:
				attrs.background = default_attrs.background;
				attrs.bg_truecolor = default_attrs.bg_truecolor;
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
			(getmode(DECOM) ? cursor.y - scroll_top : cursor.y) + 1,
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
		wmname(data);
		wmiconname(data);
	} else if (OSC_IS("1") || OSC_IS("2L")) {
		wmiconname(data);
	} else if (OSC_IS("2") || OSC_IS("21")) {
		wmname(data);
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

static void
change_color(int index, const char *name)
{
	if (index < 0 || index > 255) {
		warnx("Color index %i out of range (0..255)", index);
		return;
	}

	wmparsecolor(&palette[index], name);
}