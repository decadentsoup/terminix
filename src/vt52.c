// vt50.c - VT50 and VT52 terminal emulation
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

#include "terminix.h"

enum state {
	STATE_GROUND,
	STATE_ESCAPE,
	STATE_DCA1,
	STATE_DCA2,
	STATE_SETFG,
	STATE_SETBG
};

static enum state state;

// ESC L and ESC M have different meanings between a VT62 and the Atari VT52
// emulator. The VT62 interprets them as Enable Loop-Back Mode and Enable
// Maintenance Mode, while Atari interprets them as Insert Line and Delete Line.
// Since the later is much more useful than the former for a software terminal,
// I'm opting to favor the Atari meaning. The DEC interpretation is unlikely to
// be used by anything but maintenance tools, so this should not cause
// compatibility problems, but please let me know if it does for you.
void
vt52(unsigned char byte)
{
	switch (state) {
	case STATE_GROUND:
		if (byte == 0x1B)
			state = STATE_ESCAPE;
		else if (byte <= 0x1F || byte == 0x7F)
			execute(byte);
		else
			print(byte);
		break;
	case STATE_DCA1:
		warpto(cursor.x, byte - 0x20);
		state = STATE_DCA2;
		break;
	case STATE_DCA2:
		warpto(byte - 0x20, cursor.y);
		state = STATE_GROUND;
		break;
	case STATE_SETFG:
		cursor.attrs.foreground.r = byte & 0xF;
		cursor.attrs.fg_truecolor = false;
		state = STATE_GROUND;
		break;
	case STATE_SETBG:
		cursor.attrs.background.r = byte & 0xF;
		cursor.attrs.bg_truecolor = false;
		state = STATE_GROUND;
		break;
	case STATE_ESCAPE:
		state = STATE_GROUND;

		switch (byte) {
		case 0x3C: // < - Enter ANSI Mode
			mode[DECANM] = true;
			break;
		case 0x3D: // = - Enter Alternative Keypad Mode
			mode[DECKPAM] = true;
			break;
		case 0x3E: // > - Exit Alternative Keypad Mode
			mode[DECKPAM] = false;
			break;
		case 0x41: // A - Cursor Up
		case 0x42: // B - Cursor Down
		case 0x43: // C - Cursor Right
		case 0x44: // D - Cursor Left
			move_cursor(byte, 1);
			break;
		case 0x45: // E - Erase and Return to Home
			cursor.x = 0;
			cursor.y = 0;
			erase_display(0);
			break;
		case 0x46: // F - Enter Graphics Mode
			cursor.charset[mode[SHIFT_OUT]] = charset_vt52_graphics;
			break;
		case 0x47: // G - Exit Graphics Mode
			cursor.charset[mode[SHIFT_OUT]] = NULL;
			break;
		case 0x48: // H - Cursor to Home
			cursor.x = 0;
			cursor.y = 0;
			break;
		case 0x49: // I - Reverse Index
			revline();
			break;
		case 0x4A: // J - Erase to End of Screen
			erase_display(0);
			break;
		case 0x4B: // K - Erase to End of Line
			erase_line(0);
			break;
		case 0x4C: // L - Insert Line
			insert_line();
			break;
		case 0x4D: // M - Delete Line
			delete_line();
			break;
		case 0x52: // R - Reset
			reset();
			mode[DECANM] = false;
			break;
		case 0x54: // T - Enable Reverse Video
			cursor.attrs.negative = true;
			break;
		case 0x55: // U - Disable Reverse Video
			cursor.attrs.negative = false;
			break;
		case 0x59: // Y - Direct Cursor Address
			state = STATE_DCA1;
			break;
		case 0x5A: // Z - Identify
			ptwrite("\33/Z");
			break;
		case 0x62: // b - Set Foreground Color
			state = STATE_SETFG;
			break;
		case 0x63: // c - Set Basckground Color
			state = STATE_SETBG;
			break;
		case 0x64: // d - Erase from Upper-Left to Cursor
			warnx("TODO : erase from upper-left to cursor");
			break;
		case 0x65: // e - Show Cursor
			mode[DECTCEM] = true;
			break;
		case 0x66: // f - Hide Cursor
			mode[DECTCEM] = false;
			break;
		case 0x6A: // j - Save Cursor Position
			warnx("TODO : save cursor position");
			break;
		case 0x6B: // k - Restore Cursor Position
			warnx("TODO : restore cursor position");
			break;
		case 0x6C: // l - Move Cursor to Start of Line and Erase Line
			cursor.x = 0;
			erase_line(0);
			break;
		case 0x6F: // o - Erase from Start of Line to Cursor
			warnx("TODO : erase from start of line to cursor");
			break;
		case 0x70: // p - Enable Reverse Video
			cursor.attrs.negative = true;
			break;
		case 0x71: // q - Disable Reverse Video
			cursor.attrs.negative = false;
			break;
		case 0x76: // v - Enable Autowrap
			mode[DECAWM] = true;
			break;
		case 0x77: // w - Disable Autowrap
			mode[DECAWM] = false;
			break;
		default:
			unrecognized_escape(0, byte);
			break;
		}

		break;
	}
}