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
static void interp(long);

void
unrecognized_escape(unsigned char i0, unsigned char i1, unsigned char final)
{
	const char *name;
	char ibuf0[16], ibuf1[16], bbuf[16];

	name = getmode(DECANM) ? "ANSI" : "VT52";
	describe_byte(ibuf0, sizeof(ibuf0), i0);
	describe_byte(ibuf1, sizeof(ibuf1), i1);
	describe_byte(bbuf, sizeof(bbuf), final);

	warnx("unrecognized escape: mode=%s i0=%s i1=%s f=%s", name, ibuf0,
		ibuf1, bbuf);
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
	/*ENQ*/ case 0x05: ptwrite("%s", answerback); break;
	/*BEL*/ case 0x07: wmbell(); break;
	/*BS */ case 0x08: move_cursor(0x44, 1); break;
	/*HT */ case 0x09: tab(); break;
	/*LF */ case 0x0A:
	/*VT */ case 0x0B:
	/*FF */ case 0x0C: linefeed(); break;
	/*CR */ case 0x0D: carriagereturn(); break;
	/*SO */ case 0x0E: lockingshift(GL, G1); break;
	/*SI */ case 0x0F: lockingshift(GL, G0); break;
	/*DC1*/ case 0x11: setmode(XOFF, false); break;
	/*DC3*/ case 0x13: setmode(XOFF, true); break;
	}
}

void
vtinterp(unsigned char byte)
{
	if (!getmode(UTF8)) {
		interp(byte);
		return;
	}

	switch (sequence_size) {
	case 0:
		sequence_index = 0;
		code_point = 0;

		if (!(byte & 0x80)) {
			interp(byte);
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
			interp(0xFFFD);
		}

		break;
	case 2:
		interp(code_point | (byte & ~0xC0));
		sequence_size = 0;
		break;
	case 3:
		switch (sequence_index++) {
		case 0: code_point |= (byte & ~0xC0) << 6; break;
		default:
			interp(code_point | (byte & ~0xC0));
			sequence_size = 0;
			break;
		}
		break;
	case 4:
		switch (sequence_index++) {
		case 0: code_point |= (byte & ~0xC0) << 12; break;
		case 1: code_point |= (byte & ~0xC0) << 6; break;
		default:
			interp(code_point | (byte & ~0xC0));
			sequence_size = 0;
			break;
		}
		break;
	}
}

static void
interp(long code_point)
{
	if (getmode(DECANM))
		vt100(code_point);
	else
		vt52(code_point);
}