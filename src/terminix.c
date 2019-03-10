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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "terminix.h"

#define CHARWIDTH 8
#define CHARHEIGHT 16

#define UNUSED __attribute__((unused))

static const char *vertex_shader =
	"#version 300 es\n"
	"layout (location = 0) in vec2 vertex;\n"
	"layout (location = 1) in vec2 texcoord_in;\n"
	"out vec2 texcoord;\n"
	"void main() {\n"
		"gl_Position = vec4(vertex, 0.0, 1.0);\n"
		"texcoord = texcoord_in;\n"
	"}\n";

static const char *fragment_shader =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D texture_data;\n"
	"in vec2 texcoord;\n"
	"out vec4 color;\n"
	"void main() {\n"
		"color = texture(texture_data, texcoord);\n"
	"}\n";

static Display *display;
static Atom utf8_string, wm_delete_window, net_wm_name, net_wm_icon_name;
static Window window;
static int window_width, window_height, timer_count;
static EGLDisplay egl_display;
static EGLContext egl_context;
static EGLSurface egl_surface;
static GLuint vao, vbo, texture;

static void (*genVertexArrays)(GLsizei, GLuint *);
static void (*bindVertexArray)(GLuint);

static uint64_t get_time(void);
static void handle_exit(void);
static void init_x11(void);
static void init_egl(void);
static void init_gl(void);
static void init_shaders(void);
static GLuint compile_shader(GLenum, const char *);
static void update_size(void);
static void handle_key(XKeyEvent *);
static void render(void);
static int render_cell(unsigned char *, int, int, char, struct cell *);
static void render_glyph(unsigned char *, struct color, int, int, char, bool,
	const unsigned char *);
static void put_pixel(unsigned char *, int, int, struct color);

int
main(int argc UNUSED, char **argv UNUSED)
{
	uint64_t lasttick, currtime;
	XEvent event;

	if (atexit(handle_exit))
		pdie("failed to register exit callback");

	resize(80, 24);
	reset();
	ptinit("/bin/bash");
	init_x11();
	init_egl();
	init_gl();
	init_shaders();
	lasttick = 0;

	for (;;) {
		while ((currtime = get_time()) - lasttick > 400000000) {
			lasttick = currtime;
			timer_count++;
		}

		while (XPending(display)) {
			XNextEvent(display, &event);

			switch (event.type) {
			case KeyPress:
				handle_key(&event.xkey);
				break;
			case ClientMessage:
				if ((Atom)event.xclient.data.l[0] == wm_delete_window)
					return 0;
				break;
			case MappingNotify:
				switch (event.xmapping.request) {
				case MappingModifier:
				case MappingKeyboard:
					XRefreshKeyboardMapping(&event.xmapping);
					break;
				}
				break;
			}
		}

		ptpump();
		update_size();
		render();
	}
}

static uint64_t
get_time()
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		pdie("failed to get time");

	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void
set_window_title(const char *title)
{
	XChangeProperty(display, window, net_wm_name, utf8_string, 8,
		PropModeReplace, (const unsigned char *)title, strlen(title));
}

void
set_icon_name(const char *name)
{
	XChangeProperty(display, window, net_wm_icon_name, utf8_string, 8,
		PropModeReplace, (const unsigned char *)name, strlen(name));
}

static void
handle_exit()
{
	if (display) XCloseDisplay(display);
	if (egl_display) eglTerminate(egl_display);
	ptkill();
	deinit_screen();
}

static void
init_x11()
{
	XSetWindowAttributes attrs;
	XSizeHints *normal_hints;
	XWMHints *hints;

	if (!(display = XOpenDisplay(NULL)))
		die("failed to connect to X server");

	utf8_string = XInternAtom(display, "UTF8_STRING", false);
	wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", false);
	net_wm_name = XInternAtom(display, "_NET_WM_NAME", false);
	net_wm_icon_name = XInternAtom(display, "_NET_WM_ICON_NAME", false);

	window_width = screen_width * CHARWIDTH;
	window_height = screen_height * CHARHEIGHT;

	attrs.event_mask = KeyPressMask;

	window = XCreateWindow(display, DefaultRootWindow(display), 0, 0,
		window_width, window_height, 0, CopyFromParent, InputOutput,
		CopyFromParent, CWEventMask, &attrs);

	if (!(normal_hints = XAllocSizeHints()))
		pdie("failed to allocate XSizeHints");

	if (!(hints = XAllocWMHints())) {
		XFree(normal_hints);
		pdie("failed to allocate XWMHints");
	}

	normal_hints->flags = PMinSize|PMaxSize;
	normal_hints->min_width = window_width;
	normal_hints->min_height = window_height;
	normal_hints->max_width = window_width;
	normal_hints->max_height = window_height;

	hints->flags = InputHint|StateHint;
	hints->input = true;
	hints->initial_state = NormalState;

	XStoreName(display, window, "Terminix");
	XSetIconName(display, window, "Terminix");
	XSetWMNormalHints(display, window, normal_hints);
	XSetWMHints(display, window, hints);
	// TODO : WM_CLASS
	XSetWMProtocols(display, window, &wm_delete_window, 1);
	XMapWindow(display, window);

	XFree(hints);
	XFree(normal_hints);
}

static void
init_egl()
{
	static const EGLint cfg_attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	static const EGLint ctx_attrs[] = { EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE };

	EGLConfig config;
	EGLint num_config;

	if ((egl_display = eglGetDisplay(display)) == EGL_NO_DISPLAY)
		die("failed to get EGL display");

	if (!eglInitialize(egl_display, NULL, NULL))
		die("failed to initialize EGL");

	if (!eglChooseConfig(egl_display, cfg_attrs, &config, 1, &num_config))
		die("failed to find compatible EGL configuration");

	if (num_config != 1)
		die("failed to find compatible EGL configuration: none found");

	if ((egl_surface = eglCreateWindowSurface(egl_display, config, window, NULL)) == EGL_NO_SURFACE)
		die("failed to create EGL surface");

	if ((egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, ctx_attrs)) == EGL_NO_CONTEXT)
		die("failed to create EGL context");

	if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context))
		die("failed to make EGL context current");

	if (!(genVertexArrays = (void *)eglGetProcAddress("glGenVertexArrays")))
		die("required routine glGenVertexArrays not supported");

	if (!(bindVertexArray = (void *)eglGetProcAddress("glBindVertexArray")))
		die("required routine glBindVertexArray not supported");
}

static void
init_gl()
{
	static const GLfloat vertices[] = {
		-1, -1, 0, 1, +1, -1, 1, 1, -1, +1, 0, 0, +1, +1, 1, 0
	};

	genVertexArrays(1, &vao);
	bindVertexArray(vao);

	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void
init_shaders()
{
	GLuint vertex, fragment, program;
	GLint status;

	vertex = compile_shader(GL_VERTEX_SHADER, vertex_shader);
	fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_shader);

	if (!(program = glCreateProgram()))
		die("failed to create shader program");

	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &status);

	if (!status)
		die("failed to link shader program");

	glUseProgram(program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);
}

static GLuint
compile_shader(GLenum type, const char *source)
{
	GLuint shader;
	GLint status;

	if (!(shader = glCreateShader(type)))
		die("failed to create shader");

	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

	if (!status)
		die("failed to compile shader");

	return shader;
}

static void
update_size()
{
	int width, height;

	width = screen_width * CHARWIDTH;
	height = screen_height * CHARHEIGHT;

	if (window_width != width || window_height != height) {
		window_width = width;
		window_height = height;
		// glfwSetWindowSize(display, width, height);
		glViewport(0, 0, width, height);
	}
}

static void
handle_key(XKeyEvent *event)
{
	char buffer[32];
	int bufsize;
	KeySym keysym;

	bufsize = XLookupString(event, buffer, sizeof(buffer) - 1, &keysym, NULL);
	buffer[bufsize] = 0;

	if (mode[TRANSMIT_DISABLED])
		return;

	// TODO : mode[DECARM] - auto repeat mode

	if (bufsize > 0) {
		if (bufsize == 1 && buffer[0] == '\r' && mode[LNM])
			ptwrite("\r\n");
		else
			ptwrite("%s", buffer);

		return;
	}

	switch (keysym) {
	case XK_Insert: ptwrite("\33[2~"); return;
	case XK_Page_Up: ptwrite("\33[5~"); return;
	case XK_Page_Down: ptwrite("\33[6~"); return;
	case XK_Home: ptwrite("\33[1~"); return;
	case XK_End: ptwrite("\33[4~"); return;
	case XK_F1: ptwrite("\33OP"); return;
	case XK_F2: ptwrite("\33OQ"); return;
	case XK_F3: ptwrite("\33OR"); return;
	case XK_F4: ptwrite("\33OS"); return;
	}

	if (keysym >= XK_Left && keysym <= XK_Down) {
		if (!mode[DECANM])
			switch (keysym) {
			case XK_Up: ptwrite("\33A"); break;
			case XK_Down: ptwrite("\33B"); break;
			case XK_Right: ptwrite("\33C"); break;
			case XK_Left: ptwrite("\33D"); break;
			}
		else if (mode[DECCKM])
			switch (keysym) {
			case XK_Up: ptwrite("\33OA"); break;
			case XK_Down: ptwrite("\33OB"); break;
			case XK_Right: ptwrite("\33OC"); break;
			case XK_Left: ptwrite("\33OD"); break;
			}
		else
			switch (keysym) {
			case XK_Up: ptwrite("\33[A"); break;
			case XK_Down: ptwrite("\33[B"); break;
			case XK_Right: ptwrite("\33[C"); break;
			case XK_Left: ptwrite("\33[D"); break;
			}

		return;
	}

	// TODO : print screen, pause, f5-f25, menu (as SETUP)
	// TODO : keypad application mode
}

static void
render()
{
	static const struct color white = {0xFF, 0xFF, 0xFF};

	unsigned char buffer[window_width * window_height * 3];
	int x, y;

	for (y = screen_height - 1; y >= 0; y--)
		for (x = 0; x < screen_width;)
			x += render_cell(buffer,
				x * CHARWIDTH * (lines[y]->dimensions ? 2 : 1),
				y * CHARHEIGHT, lines[y]->dimensions,
				&lines[y]->cells[x]);

	if (mode[DECTCEM] && !(timer_count / 2 % 2))
		render_glyph(buffer, white,
			cursor.x * CHARWIDTH * (lines[cursor.y]->dimensions ? 2 : 1),
			cursor.y * CHARHEIGHT, lines[cursor.y]->dimensions,
			false, find_glyph(0x2588));

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, window_width, window_height, 0,
		GL_RGB, GL_UNSIGNED_BYTE, buffer);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	eglSwapBuffers(egl_display, egl_surface);
}

static int
render_cell(unsigned char *buffer, int px, int py, char dim, struct cell *cell)
{
	const unsigned char *glyph;
	bool dbl;
	struct color bg, fg, swap;

	if (cell->code_point)
		glyph = find_glyph(cell->code_point);
	else
		glyph = find_glyph(0x20);

	dbl = glyph[0] == 2;
	bg = cell->bg_truecolor ? cell->background : palette[cell->background.r];
	fg = cell->fg_truecolor ? cell->foreground : palette[cell->foreground.r];

	if (mode[DECSCNM] ^ cell->negative) {
		swap = bg;
		bg = fg;
		fg = swap;
	}

	render_glyph(buffer, bg, px, py, dim, dbl, find_glyph(0x2588));

	if (cell->blink == BLINK_SLOW && timer_count / 2 % 2)
		return glyph[0] == 1 ? 1 : 2;

	if (cell->blink == BLINK_FAST && timer_count % 2)
		return glyph[0] == 1 ? 1 : 2;

	if (cell->intensity == INTENSITY_FAINT) {
		fg.r /= 2;
		fg.g /= 2;
		fg.b /= 2;
	}

	render_glyph(buffer, fg, px, py, dim, false, glyph);

	if (cell->intensity == INTENSITY_BOLD)
		render_glyph(buffer, fg, px + 1, py, dim, false, glyph);

	if (cell->underline)
		render_glyph(buffer, fg, px, py, dim, dbl, find_glyph(0x0332));

	if (cell->underline == UNDERLINE_DOUBLE)
		render_glyph(buffer, fg, px, py + 2, dim, dbl, find_glyph(0x0332));

	if (cell->crossed_out)
		render_glyph(buffer, fg, px, py, dim, dbl, find_glyph(0x2015));

	if (cell->overline)
		render_glyph(buffer, fg, px, py, dim, dbl, find_glyph(0x0305));

	return dbl ? 2 : 1;
}

static void
render_glyph(unsigned char *buffer, struct color color, int px, int py,
	char dim, bool double_wide_glyph, const unsigned char *glyph)
{
	int i, imax, j, rx, ry, rxmax;

	if (!glyph)
		return;

	if (double_wide_glyph)
		render_glyph(buffer, color, px + (dim ? 16 : 8), py, dim, false,
			glyph);

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
put_pixel(unsigned char *buffer, int x, int y, struct color color)
{
	size_t i;
	if (x >= window_width) return;
	if (y >= window_height) return;
	i = (x + y * window_width) * 3;
	buffer[i++] = color.r;
	buffer[i++] = color.g;
	buffer[i  ] = color.b;
}