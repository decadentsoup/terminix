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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include "terminix.h"

int window_width, window_height, timer_count;

static Display *display;
static Atom utf8_string, wm_delete_window, net_wm_name, net_wm_icon_name;
static Window window;
static bool keystate[256];

static uint64_t get_time(void);
static void handle_exit(void);
static void init_x11(void);
static void init_xkb(void);
static void handle_key(XKeyEvent *);

int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
	uint64_t lasttick, currtime;
	XEvent event;

	if (atexit(handle_exit))
		pdie("failed to register exit callback");

	resize(80, 24);
	reset();
	ptinit("/bin/bash");
	init_x11();
	init_xkb();
	init_renderer(display, window);
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
				keystate[event.xkey.keycode] = true;
				break;
			case KeyRelease:
				keystate[event.xkey.keycode] = false;
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

void
resize_window()
{
	window_width = screen_width * CHARWIDTH;
	window_height = screen_height * CHARHEIGHT;

	if (display)
		XResizeWindow(display, window, window_width, window_height);
}

static void
handle_exit()
{
	if (display) XCloseDisplay(display);
	deinit_renderer();
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

	attrs.event_mask = KeyPressMask|KeyReleaseMask;

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
init_xkb()
{
	int major, minor;

	major = XkbMajorVersion;
	minor = XkbMinorVersion;

	if (!XkbLibraryVersion(&major, &minor))
		warnx("runtime XKB is incompatible with compile-time XKB; "
			"DECARM may not work correctly");
	if (!XkbQueryExtension(display, NULL, NULL, NULL, &major, &minor))
		warnx("X server does not support XKB extension; "
			"DECARM may not work correctly");
	else if (!XkbSetDetectableAutoRepeat(display, true, NULL))
		warnx("failed to set detectable autorepeat; "
			"DECARM may not work correctly");
}

static void
handle_key(XKeyEvent *event)
{
	char buffer[32];
	int bufsize;
	KeySym keysym;

	bufsize = XLookupString(event, buffer, sizeof(buffer) - 1, &keysym, NULL);
	buffer[bufsize] = 0;

	if (mode[TRANSMIT_DISABLED] || (!mode[DECARM] && keystate[event->keycode]))
		return;

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