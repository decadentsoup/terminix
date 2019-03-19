// terminix.c - program entry point and window management
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

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "terminix.h"

int timer_count;
uint64_t current_time;

static char *instance_name;

static void parse_command_line(int, char **);
static uint64_t get_time(void);
static void handle_exit(void);

int
main(int argc, char **argv)
{
	uint64_t lasttick;

	if (atexit(handle_exit))
		pdie("failed to register exit callback");

	if (!(setlocale(LC_ALL, "")))
		warnx("failed to set locale");

	parse_command_line(argc, argv);
	resize(80, 24);
	reset();
	ptinit("/bin/bash");
	wminit(instance_name);
	// glinit called by wminit
	lasttick = 0;

	for (;;) {
		while ((current_time = get_time()) - lasttick > 400000000) {
			lasttick = current_time;
			timer_count++;
		}

		wmpoll();
		ptpump();
		gldraw();
	}
}

static void
parse_command_line(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-name") && i < argc - 1)
			instance_name = argv[++i];

	if (!instance_name)
		instance_name = getenv("RESOURCE_NAME");

	if (!instance_name && (instance_name = strrchr(argv[0], '/')))
		instance_name++;

	if (!instance_name)
		instance_name = argv[0];
}

static uint64_t
get_time()
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		pdie("failed to get time");

	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void
handle_exit()
{
	glkill();
	wmkill();
	ptkill();
	deinit_screen();
}