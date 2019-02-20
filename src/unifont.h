// NOTE: Although this file is licensed under the same terms as Terminix,
// unifont.c is licensed under the same terms as the GNU Unifont. See
// README.adoc for a detailed explanation of what this means.

// unifont.h - unifont accessor routines
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

#ifndef UNIFONT_H
#define UNIFONT_H

extern const unsigned char *const plane0and1[];
extern const unsigned char *const plane15[];

static inline const unsigned char *
find_glyph(long code_point)
{
	if (code_point >= 0x000000 && code_point <= 0x01FFFF)
		return plane0and1[code_point];

	if (code_point >= 0x0F0000 && code_point <= 0x0FFFFF)
		return plane15[code_point - 0x0F0000];

	return 0;
}

#endif