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
	STATE_DCA2
};

static enum state state;
static unsigned char intermediate;

static void interpret(unsigned char byte);

void
vt52(const char *buffer, size_t bufsize)
{
	size_t i;

	for (i = 0; i < bufsize; i++)
		interpret(buffer[i]);
}

static void
interpret(unsigned char byte)
{
	if (byte == 0x1B) {
		state = STATE_ESCAPE;
	} else if (byte <= 0x1F || byte == 0x7F) {
		execute(byte);
	} else if (state == STATE_DCA1) {
		intermediate = byte;
		state = STATE_DCA2;
	} else if (state == STATE_DCA2) {
		warpto(byte - 0x20, intermediate - 0x20);
		state = STATE_GROUND;
	} else if (state == STATE_ESCAPE) {
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
		case 0x49: // I - Reverse Line Feed
			revline();
			break;
		case 0x4A: // J - Erase to End of Screen
			erase_display(0);
			break;
		case 0x4B: // K - Erase to End of Line
			erase_line(0);
			break;
		case 0x59: // Y - Direct Cursor Address
			state = STATE_DCA1;
			break;
		case 0x5A: // Z - Identify
			ptwrite("\33/Z");
			break;
		default:
			intermediate = 0;
			unrecognized_escape(intermediate, byte);
			break;
		}
	} else {
		print(byte);
	}
}