// terminix.c - program entry point and user interface code
// Copyright (C) 2018 Megan Ruggiero. All rights reserved.
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

#include <err.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include "ptmx.h"
#include "vtinterp.h"

#define CHARWIDTH 8
#define CHARHEIGHT 16

#define die(message) (errx(EXIT_FAILURE, "%s", message))
#define pdie(message) (err(EXIT_FAILURE, "%s", message))

#define RGB(r, g, b) { r / 255., g / 255., b / 255., 1 }
static const ALLEGRO_COLOR default_bg = RGB( 55,  55,  55);
static const ALLEGRO_COLOR default_fg = RGB(255, 255, 255);

static ALLEGRO_FONT *unifont_bmp, *unifont_smp, *unifont_csur;
static ALLEGRO_DISPLAY *display;
static ALLEGRO_TIMER *timer;
static ALLEGRO_EVENT_QUEUE *event_queue;
static int64_t timer_count;

static void handle_exit(void);
static void init_allegro(void);
static void handle_key(const ALLEGRO_KEYBOARD_EVENT *);
static void buffer_keys(const char *);
static void render(void);
static void render_cell(int, int, struct cell *);
static void mkutf8(unsigned char *, long);

int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
	unsigned char buffer[1024];
	ALLEGRO_EVENT event;

	vtreset();
	init_ptmx("/bin/bash");
	init_allegro();

	for (;;) {
		while (al_get_next_event(event_queue, &event))
			switch (event.type) {
			case ALLEGRO_EVENT_KEY_CHAR:
				handle_key(&event.keyboard);
				break;
			case ALLEGRO_EVENT_TIMER:
				timer_count = event.timer.count;
				break;
			case ALLEGRO_EVENT_DISPLAY_CLOSE:
				return 0;
			}

		vtinterp(buffer, read_ptmx(buffer, sizeof(buffer)));
		render();
	}
}

static void
handle_exit()
{
	if (event_queue) al_destroy_event_queue(event_queue);
	if (timer) al_destroy_timer(timer);
	if (display) al_destroy_display(display);
	if (unifont_csur) al_destroy_font(unifont_csur);
	if (unifont_smp) al_destroy_font(unifont_smp);
	if (unifont_bmp) al_destroy_font(unifont_bmp);
	al_uninstall_system();
	deinit_ptmx();
}

static void
init_allegro()
{
	if (atexit(handle_exit))
		pdie("failed to register atexit callback");

	if (!al_install_system(ALLEGRO_VERSION_INT, NULL))
		die("failed to initialize allegro");

	if (!al_install_keyboard())
		die("failed to initialize keyboard");

	if (!al_init_font_addon())
		die("failed to initialize font routines");

	if (!al_init_ttf_addon())
		die("failed to initialize ttf routines");

	if (!(unifont_bmp = al_load_font("unifont-bmp.ttf", 16, 0)))
		die("failed to load unifont-bmp.ttf");

	if (!(unifont_smp = al_load_font("unifont-smp.ttf", 16, 0)))
		die("failed to load unifont-smp.ttf");

	if (!(unifont_csur = al_load_font("unifont-csur.ttf", 16, 0)))
		die("failed to load unifont-csur.ttf");

	if (!(display = al_create_display(COLS * CHARWIDTH, ROWS * CHARHEIGHT)))
		die("failed to initialize display");

	if (!(timer = al_create_timer(0.4)))
		die("failed to initialize timer");

	if (!(event_queue = al_create_event_queue()))
		die("failed to initialize event queue");

	al_register_event_source(event_queue, al_get_keyboard_event_source());
	al_register_event_source(event_queue, al_get_timer_event_source(timer));
	al_register_event_source(event_queue,
		al_get_display_event_source(display));

	al_start_timer(timer);
}

static void
handle_key(const ALLEGRO_KEYBOARD_EVENT *event)
{
	char utf8[5];

	if (transmit_disabled)
		return;

	// TODO : verify home/end codes are standard
	switch (event->keycode) {
	case ALLEGRO_KEY_HOME:	buffer_keys("\33[H"); return;
	case ALLEGRO_KEY_END:	buffer_keys("\33[F"); return;
	}

	// TODO : VT52 mode
	if (mode[DECCKM])
		switch (event->keycode) {
		case ALLEGRO_KEY_LEFT:	buffer_keys("\33OD"); return;
		case ALLEGRO_KEY_RIGHT:	buffer_keys("\33OC"); return;
		case ALLEGRO_KEY_UP:	buffer_keys("\33OA"); return;
		case ALLEGRO_KEY_DOWN:	buffer_keys("\33OB"); return;
		}
	else
		switch (event->keycode) {
		case ALLEGRO_KEY_LEFT:	buffer_keys("\33[D"); return;
		case ALLEGRO_KEY_RIGHT:	buffer_keys("\33[C"); return;
		case ALLEGRO_KEY_UP:	buffer_keys("\33[A"); return;
		case ALLEGRO_KEY_DOWN:	buffer_keys("\33[B"); return;
		}

	if (event->keycode == ALLEGRO_KEY_BACKSPACE) {
		buffer_keys("\x7F");
		return;
	}

	if (event->unichar > 0) {
		mkutf8((unsigned char *)utf8, event->unichar);
		buffer_keys(utf8);
		return;
	}

	warnx("unrecognized keycode %i", event->keycode);
}

static void
buffer_keys(const char *text)
{
	write_ptmx((const unsigned char *)text, strlen(text));
}

static void
render()
{
	int x, y;

	for (y = 0; y < ROWS; y++)
		for (x = 0; x < COLS; x++)
			render_cell(x, y, &screen[y][x]);

	if (mode[DECTCEM] && timer_count / 2 % 2)
		al_draw_text(unifont_bmp, default_fg, cursor.x * CHARWIDTH,
			cursor.y * CHARHEIGHT, 0, "\u2588");

	al_flip_display();
}

static void
render_cell(int x, int y, struct cell *cell)
{
	int px, py;
	ALLEGRO_COLOR bg, fg;
	long code_point;
	char utf8[5];
	ALLEGRO_FONT *font;

	px = x * CHARWIDTH;
	py = y * CHARHEIGHT;

	if (cell->negative) { bg = default_fg; fg = default_bg; }
	else { bg = default_bg; fg = default_fg; }

	al_draw_text(unifont_bmp, bg, px, py, 0, "\u2588");

	if (cell->blink == BLINK_SLOW && timer_count / 2 % 2)
		return;

	if (cell->blink == BLINK_FAST && timer_count % 2)
		return;

	code_point = cell->code_point;
	mkutf8((unsigned char *)utf8, code_point);

	if (code_point < 0xFFFF) font = unifont_bmp;
	else if (code_point < 0x1FFFF) font = unifont_smp;
	else font = unifont_csur;

	if (cell->intensity == INTENSITY_FAINT) {
		fg.r /= 2;
		fg.g /= 2;
		fg.b /= 2;
	}

	al_draw_text(font, fg, px, py, 0, utf8);

	if (cell->intensity == INTENSITY_BOLD)
		al_draw_text(font, fg, px + 1, py, 0, utf8);

	if (cell->underline)
		al_draw_text(unifont_bmp, fg, px + CHARWIDTH, py, 0, "\u0332");

	if (cell->underline == UNDERLINE_DOUBLE)
		al_draw_text(unifont_bmp, fg, px + CHARWIDTH, py + 2, 0, "\u0332");

	if (cell->crossed_out)
		al_draw_text(unifont_bmp, fg, px, py, 0, "\u2015");

	if (cell->overline)
		al_draw_text(unifont_bmp, fg, px + CHARWIDTH, py, 0, "\u0305");
}

static void
mkutf8(unsigned char *buffer, long code_point)
{
	memset(buffer, 0, 5);

	if (code_point <= 0x7F) {
		buffer[0] = code_point;
	} else if (code_point <= 0x7FF) {
		buffer[0] = 0xC0 | (code_point >> 6);
		buffer[1] = 0x80 | (code_point & 0x3F);
	} else if (code_point <= 0xFFFF) {
		buffer[0] = 0xE0 | (code_point >> 12);
		buffer[1] = 0x80 | ((code_point >> 6) & 0x3F);
		buffer[2] = 0x80 | (code_point & 0x3F);
	} else if (code_point <= 0x10FFFF) {
		buffer[0] = 0xF0 | (code_point >> 18);
		buffer[1] = 0x80 | ((code_point >> 12) & 0x3F);
		buffer[2] = 0x80 | ((code_point >> 6) & 0x3F);
		buffer[3] = 0x80 | (code_point & 0x3F);
	} else {
		die("impossible code point");
	}
}