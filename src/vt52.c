// vt52.c - VT52 terminal emulation
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

#define self_test() (warnx("TODO : self-test"))

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
//
// The following sequences are explicitly not implemented:
// 0x4E - N - Disable Loop-Back, Raster Modes
// 0x51 - Q - Enable Raster Test
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
		case 0x31: // 1 - Enter Graph Drawing Mode
			warnx("TODO : enter graph drawing mode");
			break;
		case 0x32: // 2 - Exit Graph Drawing Mode
			warnx("TODO : disable graph drawing mode");
			break;
		case 0x3C: // < - Enter ANSI Mode
			setmode(VT52GFX, false);
			setmode(DECANM, true);
			break;
		case 0x3D: // = - Enter Alternative Keypad Mode
			setmode(DECKPAM, true);
			break;
		case 0x3E: // > - Exit Alternative Keypad Mode
			setmode(DECKPAM, false);
			break;
		case 0x41: // A - Cursor Up
		case 0x42: // B - Cursor Down
		case 0x43: // C - Cursor Right
		case 0x44: // D - Cursor Left
			if (byte == 0x42 && getmode(AUTOPRINT))
				warnx("TODO : autoprint current line");
			move_cursor(byte, 1);
			break;
		case 0x45: // E - Erase and Return to Home
			cursor.x = 0;
			cursor.y = 0;
			erase_display(0);
			break;
		case 0x46: // F - Enter Graphics Mode
			setmode(VT52GFX, true);
			break;
		case 0x47: // G - Exit Graphics Mode
			setmode(VT52GFX, false);
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
		case 0x50: // P - Self-Test
			self_test();
			break;
		case 0x52: // R - Reset
			reset();
			setmode(DECANM, false);
			break;
		case 0x53: // S - Self-Test
			self_test();
			break;
		case 0x54: // T - Enable Reverse Video
			cursor.attrs.negative = true;
			break;
		case 0x55: // U - Disable Reverse Video
			cursor.attrs.negative = false;
			break;
		case 0x56: // V - Print Line
			warnx("TODO : print current line");
			break;
		case 0x57: // W - Enable Printer-Controller Mode
			// TODO : start redirecting data directly to the print
			// backend except for XON and XOFF; if ESC X is
			// received, send ESC CAN (cancel) to the print backend
			// and disable printer-controller mode
			break;
		case 0x58: // X - Disable Printer-Controller Mode
			// Already disabled, so just eat the byte.
			break;
		case 0x59: // Y - Direct Cursor Address
			state = STATE_DCA1;
			break;
		case 0x5A: // Z - Identify
			ptwrite("\33/Z");
			break;
		case 0x5B: // [ - Enable Hold Screen Mode
			warnx("TODO : Enable Hold Screen Mode");
			break;
		case 0x5C: // \ - Disable Hold Screen Mode
			warnx("TODO : Disable Hold Screen Mode");
			break;
		case 0x5D: // ] - Print Screen
			warnx("TODO : print from top of screen to current line");
			break;
		case 0x5E: // ^ - Enable Auto-Print Mode
			setmode(AUTOPRINT, true);
			break;
		case 0x5F: // _ - Disable Auto-Print Mode
			setmode(AUTOPRINT, false);
			break;
		case 0x62: // b - Set Foreground Color
			state = STATE_SETFG;
			break;
		case 0x63: // c - Set Background Color
			state = STATE_SETBG;
			break;
		case 0x64: // d - Erase from Upper-Left to Cursor
			erase_display(1);
			break;
		case 0x65: // e - Show Cursor
			setmode(DECTCEM, true);
			break;
		case 0x66: // f - Hide Cursor
			setmode(DECTCEM, false);
			break;
		case 0x6A: // j - Save Cursor Position
			saved_cursor = cursor;
			break;
		case 0x6B: // k - Restore Cursor Position
			cursor.x = saved_cursor.x;
			cursor.y = saved_cursor.y;
			cursor.last_column = saved_cursor.last_column;
			break;
		case 0x6C: // l - Move Cursor to Start of Line and Erase Line
			cursor.x = 0;
			erase_line(0);
			break;
		case 0x6F: // o - Erase from Start of Line to Cursor
			erase_line(1);
			break;
		case 0x70: // p - Enable Reverse Video
			cursor.attrs.negative = true;
			break;
		case 0x71: // q - Disable Reverse Video
			cursor.attrs.negative = false;
			break;
		case 0x76: // v - Enable Autowrap
			setmode(DECAWM, true);
			break;
		case 0x77: // w - Disable Autowrap
			setmode(DECAWM, false);
			break;
		default:
			unrecognized_escape(0, 0, byte);
			break;
		}

		break;
	}
}