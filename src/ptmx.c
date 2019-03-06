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
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "terminix.h"

#define die(message) (errx(EXIT_FAILURE, "%s", message))
#define pdie(message) (err(EXIT_FAILURE, "%s", message))
#define pdiec(message) (pdie("[child] " message))

static int ptmx = -1;
static unsigned char write_buffer[1024];
static size_t write_buffer_size;

static void set_nonblock(void);
static _Noreturn void init_child(const char *, const char *);
static void read_ptmx(void);
static void flush_ptmx(void);

void
init_ptmx(const char *shell)
{
	const char *pts;

	if ((ptmx = posix_openpt(O_RDWR|O_NOCTTY)) < 0)
		pdie("failed to open parent pseudoterminal");

	set_nonblock();

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
set_nonblock()
{
	int flags;

	if ((flags = fcntl(ptmx, F_GETFL)) < 0)
		pdie("failed to get pseudoterminal flags");

	if ((fcntl(ptmx, F_SETFL, flags|O_NONBLOCK)) == -1)
		pdie("failed to set pseudoterminal flags to non-blocking");
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

void
pump_ptmx()
{
	struct pollfd pfd;
	bool loop;

	pfd.fd = ptmx;
	pfd.events = POLLIN|POLLOUT;

	for (;;)
		switch (poll(&pfd, 1, 0)) {
		case 0:
			warnx("0");
			return;
		case 1:
			if (pfd.revents & POLLERR)
				die("pseudoterminal is broken");

			if (pfd.revents & POLLHUP)
				exit(0);

			if (pfd.revents & POLLNVAL)
				die("pseudoterminal not open");

			loop = false;

			if (pfd.revents & POLLIN) {
				read_ptmx();
				loop = true;
			}

			if (pfd.revents & POLLOUT && write_buffer_size) {
				flush_ptmx();
				loop = true;
			}

			if (loop)
				break;

			return;
		default:
			pdie("failed to poll pseudoterminal");
		}
}

static void
read_ptmx()
{
	unsigned char buffer[1024];
	ssize_t n;

	if ((n = read(ptmx, buffer, sizeof(buffer))) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return;
		pdie("failed to read parent pseudoterminal");
	}

	vtinterp(buffer, n);
}

static void
flush_ptmx()
{
	ssize_t n;

	if ((n = write(ptmx, write_buffer, write_buffer_size)) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return;
		pdie("failed to write to parent pseudoterminal");
	}

	if (n > 0) {
		write_buffer_size -= n;
		memmove(write_buffer, write_buffer + n, write_buffer_size);
	}
}