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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "ptmx.h"
#include "screen.h"
#include "unifont.h"
#include "vtinterp.h"

#define CHARWIDTH 8
#define CHARHEIGHT 16

#define UNUSED __attribute__((unused))
#define die(message) (errx(EXIT_FAILURE, "%s", message))
#define pdie(message) (err(EXIT_FAILURE, "%s", message))

static int display_width, display_height;
static GLFWwindow *display;
static GLuint texture;
static int timer_count;

static void handle_exit(void);
static void handle_glfw_error(int, const char *);
static void handle_opengl_debug(GLenum, GLenum, GLuint, GLenum, GLsizei,
	const GLchar *, const void *);
static void init_glfw(void);
static void update_size(void);
static void handle_key(GLFWwindow *, int, int, int, int);
static void handle_char(GLFWwindow *, unsigned int);
static void buffer_keys(const char *);
static void render(void);
static int render_cell(GLuint *, int, int, char, struct cell *);
static void render_glyph(GLuint *, GLuint, int, int, char, const unsigned char *);
static void put_pixel(GLuint *, int, int, GLuint);

int
main(int argc UNUSED, char **argv UNUSED)
{
	unsigned char buffer[1024];
	double lasttick, currtime;

	resize(80, 24);
	reset();
	init_ptmx("/bin/bash");
	init_glfw();

	while (!glfwWindowShouldClose(display)) {
		while ((currtime = glfwGetTime()) - lasttick - 0.4 > 0) {
			lasttick = currtime;
			timer_count++;
		}

		glfwPollEvents();
		vtinterp(buffer, read_ptmx(buffer, sizeof(buffer)));
		update_size();
		render();
	}
}

static void
handle_exit()
{
	glfwDestroyWindow(display);
	glfwTerminate();
	deinit_ptmx();
	deinit_screen();
}

static void
handle_glfw_error(int error UNUSED, const char *description)
{
	warnx("GLFW: %s", description);
}

static void
handle_opengl_debug(GLenum source UNUSED, GLenum type UNUSED, GLuint id UNUSED,
	GLenum severity UNUSED, GLsizei length UNUSED, const GLchar *message,
	const void *param UNUSED)
{
	warnx("OpenGL: %s", message);
}

static void
init_glfw()
{
	GLenum status;

	if (atexit(handle_exit))
		pdie("failed to register atexit callback");

	glfwSetErrorCallback(handle_glfw_error);

	if (!glfwInit())
		die("failed to initialize GLFW");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_RESIZABLE, false);

	display_width = screen_width * CHARWIDTH;
	display_height = screen_height * CHARHEIGHT;
	if (!(display = glfwCreateWindow(display_width, display_height,
		"Terminix", NULL, NULL)))
		die("failed to create main window");

	glfwSetKeyCallback(display, handle_key);
	glfwSetCharCallback(display, handle_char);
	glfwMakeContextCurrent(display);
	glfwSwapInterval(1);

	if ((status = glewInit()))
		errx(EXIT_FAILURE, "GLEW: %s", glewGetErrorString(status));

	glDebugMessageCallback(handle_opengl_debug, NULL);
	glEnable(GL_DEBUG_OUTPUT);

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glEnable(GL_TEXTURE_2D);
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
		glfwSetWindowSize(display, width, height);
	}
}

static void
handle_key(GLFWwindow *window UNUSED, int key, int scancode UNUSED, int action,
	int mods)
{
	if (action == GLFW_RELEASE || mode[TRANSMIT_DISABLED])
		return;

	if (!mode[DECARM] && action == GLFW_REPEAT)
		return;

	switch (key) {
	case GLFW_KEY_ESCAPE: buffer_keys("\33"); return;
	case GLFW_KEY_ENTER:
	case GLFW_KEY_KP_ENTER:
		buffer_keys(mode[LNM] ? "\r\n" : "\r");
		return;
	case GLFW_KEY_TAB: buffer_keys("\t"); return;
	case GLFW_KEY_BACKSPACE: buffer_keys("\b"); return;
	case GLFW_KEY_INSERT: buffer_keys("\33[2~"); return;
	case GLFW_KEY_DELETE: buffer_keys("\177"); return;
	case GLFW_KEY_PAGE_UP: buffer_keys("\33[5~"); return;
	case GLFW_KEY_PAGE_DOWN: buffer_keys("\33[6~"); return;
	case GLFW_KEY_HOME: buffer_keys("\33[1~"); return;
	case GLFW_KEY_END: buffer_keys("\33[4~"); return;
	case GLFW_KEY_F1: buffer_keys("\33OP"); return;
	case GLFW_KEY_F2: buffer_keys("\33OQ"); return;
	case GLFW_KEY_F3: buffer_keys("\33OR"); return;
	case GLFW_KEY_F4: buffer_keys("\33OS"); return;
	}

	if (key >= GLFW_KEY_SPACE && key <= GLFW_KEY_GRAVE_ACCENT) {
		if (mods & GLFW_MOD_CONTROL) {
			key &= ~0x60;

			if (mods & GLFW_MOD_SHIFT)
				key ^= key & 0x40 ? 0x20 : 0x10;

			char buffer[2] = {key, 0};
			buffer_keys(buffer);
		}

		return;
	}

	if (key >= GLFW_KEY_RIGHT && key <= GLFW_KEY_UP) {
		if (!mode[DECANM])
			switch (key) {
			case GLFW_KEY_UP: buffer_keys("\33A"); break;
			case GLFW_KEY_DOWN: buffer_keys("\33B"); break;
			case GLFW_KEY_RIGHT: buffer_keys("\33C"); break;
			case GLFW_KEY_LEFT: buffer_keys("\33D"); break;
			}
		else if (mode[DECCKM])
			switch (key) {
			case GLFW_KEY_UP: buffer_keys("\33OA"); break;
			case GLFW_KEY_DOWN: buffer_keys("\33OB"); break;
			case GLFW_KEY_RIGHT: buffer_keys("\33OC"); break;
			case GLFW_KEY_LEFT: buffer_keys("\33OD"); break;
			}
		else
			switch (key) {
			case GLFW_KEY_UP: buffer_keys("\33[A"); break;
			case GLFW_KEY_DOWN: buffer_keys("\33[B"); break;
			case GLFW_KEY_RIGHT: buffer_keys("\33[C"); break;
			case GLFW_KEY_LEFT: buffer_keys("\33[D"); break;
			}

		return;
	}

	// TODO : GLFW_KEY_UNKNOWN
	// TODO : print screen, pause, f5-f25, menu (as SETUP)
	// TODO : keypad application mode
}

static void
handle_char(GLFWwindow *window UNUSED, unsigned int code_point)
{
	char buffer[5];

	memset(buffer, 0, sizeof(buffer));

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

	buffer_keys(buffer);
}

static void
buffer_keys(const char *text)
{
	write_ptmx((const unsigned char *)text, strlen(text));
}

static void
render()
{
	GLuint buffer[display_width * display_height];
	int x, y, width, height;

	for (y = 0; y < screen_height; y++)
		for (x = 0; x < screen_width;)
			x += render_cell(buffer,
				x * CHARWIDTH * (lines[y].dimensions ? 2 : 1),
				y * CHARHEIGHT, lines[y].dimensions,
				&screen[x + y * screen_width]);

	if (mode[DECTCEM] && timer_count / 2 % 2)
		render_glyph(buffer, 0xFFFFFFFF,
			cursor.x * CHARWIDTH * (lines[y].dimensions ? 2 : 1),
			cursor.y * CHARHEIGHT, lines[cursor.y].dimensions,
			find_glyph(0x2588));

	glfwGetFramebufferSize(display, &width, &height);
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, height, 0, -1, 1);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, display_width, display_height,
		0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, buffer);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(width, 0);
	glTexCoord2f(1, 1); glVertex2f(width, height);
	glTexCoord2f(0, 1); glVertex2f(0, height);
	glEnd();

	glfwSwapBuffers(display);
}

static int
render_cell(GLuint *buffer, int px, int py, char dim, struct cell *cell)
{
	static const GLuint DEFAULT_BG = 0x373737FF;
	static const GLuint DEFAULT_FG = 0xFFFFFFFF;

	const unsigned char *glyph;
	GLuint bg, fg;

	if (cell->code_point)
		glyph = find_glyph(cell->code_point);
	else
		glyph = find_glyph(0x20);

	if (mode[DECSCNM] ^ cell->negative)
		{ bg = DEFAULT_FG; fg = DEFAULT_BG; }
	else
		{ bg = DEFAULT_BG; fg = DEFAULT_FG; }

	render_glyph(buffer, bg, px, py, dim, find_glyph(0x2588));

	if (glyph[0] == 2)
		render_glyph(buffer, bg, px + 8, py, dim, find_glyph(0x2588));

	if (cell->blink == BLINK_SLOW && timer_count / 2 % 2)
		return glyph[0] == 1 ? 1 : 2;

	if (cell->blink == BLINK_FAST && timer_count % 2)
		return glyph[0] == 1 ? 1 : 2;

	if (cell->intensity == INTENSITY_FAINT)
		fg = (fg >> 24) / 2 << 24 | ((fg >> 16) & 0xFF) / 2 << 16 |
			((fg >> 8) & 0xFF) / 2 << 8 | 0xFF;

	render_glyph(buffer, fg, px, py, dim, glyph);

	if (cell->intensity == INTENSITY_BOLD)
		render_glyph(buffer, fg, px + 1, py, dim, glyph);

	if (cell->underline)
		render_glyph(buffer, fg, px, py, dim, find_glyph(0x0332));

	if (cell->underline == UNDERLINE_DOUBLE)
		render_glyph(buffer, fg, px, py + 2, dim, find_glyph(0x0332));

	if (cell->crossed_out)
		render_glyph(buffer, fg, px, py, dim, find_glyph(0x2015));

	if (cell->overline)
		render_glyph(buffer, fg, px, py, dim, find_glyph(0x0305));

	return glyph[0] == 1 ? 1 : 2;
}

static void
render_glyph(GLuint *buffer, GLuint color, int px, int py, char dim, const unsigned char *glyph)
{
	int i, imax, j, rx, ry, rxmax;

	if (!glyph)
		return;

	i = 1;
	imax = glyph[0] == 1 ? 17 : 33;

	if (dim) {
		switch (dim) {
		case DOUBLE_HEIGHT_TOP: imax = imax / 2 + 1; break;
		case DOUBLE_HEIGHT_BOTTOM: i = imax / 2 + 1; break;
		}
	}

	rxmax = (glyph[0] == 1 ? 8 : 16) * (dim ? 2 : 1) - 1;

	for (rx = 0, ry = 0; i < imax; i++) {
		for (j = 0; j < 8; j++) {
			if ((glyph[i] << j) & 0x80) {
				put_pixel(buffer, px + rx, py + ry, color);

				if (dim) {
					put_pixel(buffer, px + rx + 1, py + ry, color);

					if (dim > DOUBLE_WIDTH) {
						put_pixel(buffer, px + rx, py + ry + 1, color);
						put_pixel(buffer, px + rx + 1, py + ry + 1, color);
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
put_pixel(GLuint *buffer, int x, int y, GLuint color)
{
	if (x > display_width) return;
	if (y > display_height) return;
	buffer[x + y * display_width] = color;
}