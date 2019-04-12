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

#define _XOPEN_SOURCE 600 // pseudoterminals
#define _GNU_SOURCE // vasprintf
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "terminix.h"

#define pdiec(message) (pdie("[child] " message))

static int ptmx = -1;
static unsigned char write_buffer[1024];
static size_t write_buffer_size;

static void set_nonblock(void);
static _Noreturn void init_child(const char *);
static void read_ptmx(void);
static void flush_ptmx(void);

void
ptinit()
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
		init_child(pts);
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
init_child(const char *pts)
{
	const struct passwd *passwd;
	const char *shell;

	if (setsid() < 0) pdiec("failed to create session");

	if (close(0)) pdiec("failed to close standard input");
	if (close(1)) pdiec("failed to close standard output");
	if (close(2)) pdiec("failed to close standard error");
	if (close(ptmx)) pdiec("failed to close parent pseudoterminal");

	if (open(pts, O_RDWR) < 0)
		pdiec("failed to open pseudoterminal");

	if (dup(0) < 0) pdiec("failed to dup pseudoterminal (1)");
	if (dup(0) < 0) pdiec("failed to dup pseudoterminal (2)");

	if (!(shell = getenv("SHELL"))) {
		errno = 0;

		if (!(passwd = getpwuid(getuid()))) {
			if (errno)
				warn("failed to get user's default shell");
			else
				warn("you don't exist");

			shell = "/bin/sh";
		} else {
			shell = passwd->pw_shell[0]?passwd->pw_shell:"/bin/sh";
		}
	}

	unsetenv("COLUMNS");
	unsetenv("LINES");
	unsetenv("SHELL");
	unsetenv("TERMCAP");

	setenv("TERM", "vt100", 1);

	execl(shell, shell, NULL);
	err(EXIT_FAILURE, "[child] failed to execute %s", shell);
}

void
ptkill()
{
	if (ptmx >= 0 && close(ptmx))
		warn("failed to close parent pseudoterminal");
}

void
ptbreak(bool shift)
{
	warnx("TODO : BREAK for %s seconds +/- 10%%", shift ? "3.5" : "0.2333");
}

void
ptwrite(const char *format, ...)
{
	va_list args;
	char *buffer;
	int i, bufsize;

	va_start(args, format);

	if ((bufsize = vasprintf(&buffer, format, args)) == -1)
		pdie("failed to format string");

	va_end(args);

	if ((size_t)bufsize <= sizeof(write_buffer) - write_buffer_size) {
		for (i = 0; i < bufsize; i++)
			write_buffer[write_buffer_size++] = buffer[i];

		if (write_buffer_size > 0)
			flush_ptmx();
	}

	free(buffer);
}

void
ptpump()
{
	struct pollfd pfd;
	bool loop;

	pfd.fd = ptmx;
	pfd.events = POLLIN|POLLOUT;

	for (;;)
		switch (poll(&pfd, 1, 0)) {
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
	char buffer[1024];
	ssize_t i, n;

	if ((n = read(ptmx, buffer, sizeof(buffer))) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return;
		pdie("failed to read parent pseudoterminal");
	}

	for (i = 0; i < n; i++)
		if (mode[DECANM])
			vt100(buffer[i]);
		else
			vt52(buffer[i]);
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