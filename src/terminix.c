// terminix.c - program entry point and user interface code
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

#include <err.h>
#include <allegro5/allegro.h>
#include "ptmx.h"
#include "unifont.h"
#include "vtinterp.h"

#define CHARWIDTH 8
#define CHARHEIGHT 16

#define die(message) (errx(EXIT_FAILURE, "%s", message))
#define pdie(message) (err(EXIT_FAILURE, "%s", message))

#define RGB(r, g, b) { r / 255., g / 255., b / 255., 1 }
static const ALLEGRO_COLOR default_bg = RGB( 55,  55,  55);
static const ALLEGRO_COLOR default_fg = RGB(255, 255, 255);

static int display_width, display_height;
static ALLEGRO_DISPLAY *display;
static ALLEGRO_TIMER *timer;
static ALLEGRO_EVENT_QUEUE *event_queue;
static int64_t timer_count;

static void handle_exit(void);
static void init_allegro(void);
static void update_size(void);
static void handle_key(const ALLEGRO_KEYBOARD_EVENT *);
static void buffer_keys(const char *);
static void render(void);
static int render_cell(int, int, char, struct cell *);
static void render_glyph(ALLEGRO_COLOR, int, int, char, const unsigned char *);
static void mkutf8(unsigned char *, long);

int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
	unsigned char buffer[1024];
	ALLEGRO_EVENT event;

	vtreset();
	vtresize(80, 24);
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
		update_size();
		render();
	}
}

static void
handle_exit()
{
	if (event_queue) al_destroy_event_queue(event_queue);
	if (timer) al_destroy_timer(timer);
	if (display) al_destroy_display(display);
	al_uninstall_system();
	deinit_ptmx();
	vtcleanup();
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

	display_width = screen_width * CHARWIDTH;
	display_height = screen_height * CHARHEIGHT;
	if (!(display = al_create_display(display_width, display_height)))
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
update_size()
{
	int width, height;

	width = screen_width * CHARWIDTH;
	height = screen_height * CHARHEIGHT;

	if (display_width != width || display_height != height) {
		display_width = width;
		display_height = height;
		al_resize_display(display, width, height);
	}
}

static void
handle_key(const ALLEGRO_KEYBOARD_EVENT *event)
{
	char utf8[5];

	if (mode[TRANSMIT_DISABLED])
		return;

	if (!mode[DECARM] && event->repeat)
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
	ALLEGRO_BITMAP *backbuffer;
	int x, y;

	backbuffer = al_get_backbuffer(display);
	if (!al_lock_bitmap(backbuffer, ALLEGRO_PIXEL_FORMAT_ANY,
		ALLEGRO_LOCK_WRITEONLY))
		die("failed to lock display backbuffer");

	for (y = 0; y < screen_height; y++)
		for (x = 0; x < screen_width;)
			x += render_cell(x * CHARWIDTH, y * CHARHEIGHT,
				lines[y].dimensions,
				&screen[x + y * screen_width]);

	if (mode[DECTCEM] && timer_count / 2 % 2)
		render_glyph(default_fg, cursor.x * CHARWIDTH,
			cursor.y * CHARHEIGHT, 0, find_glyph(0x2588));

	al_unlock_bitmap(backbuffer);
	al_flip_display();
}

static int
render_cell(int px, int py, char dim, struct cell *cell)
{
	const unsigned char *glyph;
	ALLEGRO_COLOR bg, fg;

	if (cell->code_point)
		glyph = find_glyph(cell->code_point);
	else
		glyph = find_glyph(0x20);

	if (mode[DECSCNM] ^ cell->negative)
		{ bg = default_fg; fg = default_bg; }
	else
		{ bg = default_bg; fg = default_fg; }

	render_glyph(bg, px, py, dim, find_glyph(0x2588));

	if (glyph[0] == 2)
		render_glyph(bg, px + 8, py, dim, find_glyph(0x2588));

	if (cell->blink == BLINK_SLOW && timer_count / 2 % 2)
		return glyph[0] == 1 ? 1 : 2;

	if (cell->blink == BLINK_FAST && timer_count % 2)
		return glyph[0] == 1 ? 1 : 2;

	if (cell->intensity == INTENSITY_FAINT) {
		fg.r /= 2;
		fg.g /= 2;
		fg.b /= 2;
	}

	render_glyph(fg, px, py, dim, glyph);

	if (cell->intensity == INTENSITY_BOLD)
		render_glyph(fg, px + 1, py, dim, glyph);

	if (cell->underline)
		render_glyph(fg, px + CHARWIDTH, py, dim, find_glyph(0x0332));

	if (cell->underline == UNDERLINE_DOUBLE)
		render_glyph(fg, px + CHARWIDTH, py + 2,dim,find_glyph(0x0332));

	if (cell->crossed_out)
		render_glyph(fg, px, py, dim, find_glyph(0x2015));

	if (cell->overline)
		render_glyph(fg, px + CHARWIDTH, py, dim, find_glyph(0x0305));

	return glyph[0] == 1 ? 1 : 2;
}


static void
render_glyph(ALLEGRO_COLOR color, int px, int py, char dim,
	const unsigned char *glyph)
{
	int i, imax, j, rx, ry, rxmax;

	if (!glyph)
		return;

	i = 1;
	imax = glyph[0] == 1 ? 17 : 33;

	if (dim) {
		px *= 2;

		switch (dim) {
		case DOUBLE_HEIGHT_TOP: imax = imax / 2 + 1; break;
		case DOUBLE_HEIGHT_BOTTOM: i = imax / 2 + 1; break;
		}
	}

	rxmax = (glyph[0] == 1 ? 8 : 16) * (dim ? 2 : 1) - 1;

	for (rx = 0, ry = 0; i < imax; i++) {
		for (j = 0; j < 8; j++) {
			if ((glyph[i] << j) & 0x80) {
				al_put_pixel(px + rx, py + ry, color);

				if (dim) {
					al_put_pixel(px + rx + 1, py + ry, color);

					if (dim > DOUBLE_WIDTH) {
						al_put_pixel(px + rx, py + ry + 1, color);
						al_put_pixel(px + rx + 1, py + ry + 1, color);
					}
				}
			}

			if ((rx += dim ? 2 : 1) > rxmax) {
				rx = 0;
				ry += dim > DOUBLE_WIDTH ? 2 : 1;
			}
		}
	}
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