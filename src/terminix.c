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

#include <getopt.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "terminix.h"

char *instance_name;
const char *answerback = "";
float opacity = 1.0, glow = 0.0, static_ = 0.0, glow_line = 0.0,
	glow_line_speed = 4.0;

int timer_count;
uint64_t current_time;

static void parse_command_line(int, char **);
static float parse_percentage(const char *);
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
	ptinit();
	wminit();
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
	enum { HELP = 1, VERSION, NAME, ANSWERBACK, OPACITY, GLOW, STATIC,
		GLOW_LINE, GLOW_LINE_SPEED };

	static const struct option options[] = {
		{ "help", no_argument, 0, HELP },
		{ "version", no_argument, 0, VERSION },
		{ "name", required_argument, 0, NAME },
		{ "answerback", required_argument, 0, ANSWERBACK },
		{ "opacity", required_argument, 0, OPACITY },
		{ "glow", required_argument, 0, GLOW },
		{ "static", required_argument, 0, STATIC },
		{ "glow-line", required_argument, 0, GLOW_LINE },
		{ "glow-line-speed", required_argument, 0, GLOW_LINE_SPEED },
		{ 0, 0, 0, 0 }
	};

	int opt;
	bool badopt;

	badopt = false;

	while ((opt = getopt_long_only(argc, argv, "", options, NULL)) != -1)
		switch (opt) {
		case HELP:
			die("TODO : help not yet implemented");
		case VERSION:
			fputs("terminix " PKGVER "\n", stderr);
			exit(EXIT_SUCCESS);
		case NAME:
			instance_name = optarg;
			break;
		case ANSWERBACK:
			answerback = optarg;
			break;
		case OPACITY:
			opacity = parse_percentage(optarg);
			break;
		case GLOW:
			glow = parse_percentage(optarg);
			break;
		case STATIC:
			static_ = parse_percentage(optarg);
			break;
		case GLOW_LINE:
			glow_line = parse_percentage(optarg);
			break;
		case GLOW_LINE_SPEED:
			glow_line_speed = atof(optarg);
			break;
		case '?':
			badopt = true;
			break;
		}

	if (badopt)
		die("bad command line arguments; aborting...");

	if (!instance_name)
		instance_name = getenv("RESOURCE_NAME");

	if (!instance_name && (instance_name = strrchr(argv[0], '/')))
		instance_name++;

	if (!instance_name)
		instance_name = argv[0];
}

static float
parse_percentage(const char *argument)
{
	if (strchr(argument, '%'))
		return atof(argument) / 100.0;

	return atof(argument);
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