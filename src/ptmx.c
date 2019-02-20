// ptmx.c - pseudoterminal manipulation routines
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

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ptmx.h"
#include "vtinterp.h"

#define die(message) (errx(EXIT_FAILURE, "%s", message))
#define pdie(message) (err(EXIT_FAILURE, "%s", message))
#define pdiec(message) (pdie("[child] " message))

static int ptmx = -1;
static unsigned char write_buffer[1024];
static size_t write_buffer_size;

static _Noreturn void init_child(const char *, const char *);
static void flush_ptmx(void);

void
init_ptmx(const char *shell)
{
	const char *pts;

	// TODO:standards compliant?
	if ((ptmx = posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK)) < 0)
		pdie("failed to open parent pseudoterminal");

	if (grantpt(ptmx))
		pdie("failed to set permissions on child pseudoterminal");

	if (unlockpt(ptmx))
		pdie("failed to unlock child pseudoterminal");

	if (!(pts = ptsname(ptmx)))
		pdie("failed to get name of child pseudoterminal");

	switch (fork()) {
	case -1:
		pdie("failed to create child process");
	case 0:
		init_child(pts, shell);
	}
}

static void
init_child(const char *pts, const char *shell)
{
	if (setsid() < 0) pdiec("failed to create session");

	if (close(0)) pdiec("failed to close standard input");
	if (close(1)) pdiec("failed to close standard output");
	if (close(2)) pdiec("failed to close standard error");
	if (close(ptmx)) pdiec("failed to close parent pseudoterminal");

	if (open(pts, O_RDWR) < 0)
		pdiec("failed to open pseudoterminal");

	if (dup(0) < 0) pdiec("failed to dup pseudoterminal (1)");
	if (dup(0) < 0) pdiec("failed to dup pseudoterminal (2)");

	unsetenv("COLUMNS");
	unsetenv("LINES");
	unsetenv("TERMCAP");

	setenv("SHELL", shell, 1);
	setenv("TERM", "vt100", 1);

	execl(shell, shell, NULL);
	err(EXIT_FAILURE, "[child] failed to execute %s", shell);
}

void
deinit_ptmx()
{
	if (ptmx >= 0 && close(ptmx))
		warn("failed to close parent pseudoterminal");
}

void
write_ptmx_num(unsigned int num)
{
	unsigned char buffer[5];
	int i;

	if (num > 65535)
		die("tried to write a number that's too big");

	if (num) {
		for (i = sizeof(buffer) - 1; num; num /= 10) {
			buffer[i--] = 0x30 + num % 10;
		}

		i++;
	} else {
		buffer[i = sizeof(buffer) - 1] = 0x30;
	}

	write_ptmx(&buffer[i], sizeof(buffer) - i);
}

void
write_ptmx(const unsigned char *buffer, size_t bufsize)
{
	size_t i;

	if (bufsize > sizeof(write_buffer) - write_buffer_size)
		return; // not enough room

	for (i = 0; i < bufsize; i++)
		write_buffer[write_buffer_size++] = buffer[i];

	if (write_buffer_size > 0)
		flush_ptmx();
}

static void
flush_ptmx()
{
	ssize_t n;

	if ((n = write(ptmx, write_buffer, write_buffer_size)) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;

		pdie("failed to write to parent pseudoterminal");
	}

	if (n > 0) {
		write_buffer_size -= n;
		memmove(write_buffer, write_buffer + n, write_buffer_size);
	}
}

size_t
read_ptmx(unsigned char *buffer, size_t bufsize)
{
	ssize_t n;

	if ((n = read(ptmx, buffer, bufsize)) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;

		pdie("failed to read parent pseudoterminal");
	}

	return n;
}