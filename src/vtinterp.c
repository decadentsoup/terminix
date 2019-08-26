// vtinterp.c - common emulator routines
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
#include "terminix.h"

static char sequence_size, sequence_index;
static long code_point;

static void describe_byte(char *, size_t, unsigned char);

void
unrecognized_escape(unsigned char intermediate, unsigned char byte)
{
	const char *name;
	char ibuf[16], bbuf[16];

	name = mode[DECANM] ? "ANSI" : "VT52";
	describe_byte(ibuf, sizeof(ibuf), intermediate);
	describe_byte(bbuf, sizeof(bbuf), byte);

	warnx("unrecognized escape: mode=%s intermediate=%s byte=%s", name, ibuf, bbuf);
}

static void
describe_byte(char *buffer, size_t bufsize, unsigned char byte)
{
	if (isprint(byte))
		snprintf(buffer, bufsize, "\"%c\"", byte);
	else
		snprintf(buffer, bufsize, "0x%X", byte);
}

void
execute(unsigned char byte)
{
	switch (byte) {
	case 0x05: // Enquiry
		ptwrite("%s", answerback);
		break;
	case 0x07: // Bell
		wmbell();
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
		if (mode[AUTOPRINT]) warnx("TODO : autoprint current line");
		newline();
		if (mode[LNM]) cursor.x = 0;
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

void
print(unsigned char byte)
{
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