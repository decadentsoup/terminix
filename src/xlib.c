// xlib.c - window management routines
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
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include "terminix.h"

int window_width, window_height;

static Display *display;
static Atom utf8_string, wm_protocols, wm_delete_window, net_wm_name,
	net_wm_icon_name;
static Window window;
static XIM xim;
static XIC xic;
static bool keystate[256];

static void init_x11(char *instance_name);
static void init_xkb(void);
static void init_xim(void);
static void handle_key(XKeyEvent *);
static void kpam(char);

void
wminit(char *instance_name)
{
	init_x11(instance_name);
	init_xkb();
	init_xim();

	glinit(display, window);
}

void
wmkill()
{
	if (xic) XDestroyIC(xic);
	if (xim) XCloseIM(xim);
	if (display) XCloseDisplay(display);
}

void
wmpoll()
{
	XEvent event;

	while (XPending(display)) {
		XNextEvent(display, &event);

		if (XFilterEvent(&event, None))
			continue;

		switch (event.type) {
		case KeyPress:
			handle_key(&event.xkey);
			keystate[event.xkey.keycode] = true;
			break;
		case KeyRelease:
			keystate[event.xkey.keycode] = false;
			break;
		case FocusIn:
			XSetICFocus(xic);
			break;
		case FocusOut:
			XUnsetICFocus(xic);
			break;
		case ClientMessage:
			if (event.xclient.message_type == wm_protocols && (Atom)event.xclient.data.l[0] == wm_delete_window)
				exit(EXIT_SUCCESS);
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

void
ring_bell()
{
	XBell(display, 0);
}

static void
init_x11(char *instance_name)
{
	XVisualInfo visual_info;
	XSetWindowAttributes attrs;
	XSizeHints *normal_hints;
	XWMHints *hints;
	XClassHint *class_hint;

	if (!(display = XOpenDisplay(NULL)))
		die("failed to connect to X server");

	utf8_string = XInternAtom(display, "UTF8_STRING", false);
	wm_protocols = XInternAtom(display, "WM_PROTOCOLS", false);
	wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", false);
	net_wm_name = XInternAtom(display, "_NET_WM_NAME", false);
	net_wm_icon_name = XInternAtom(display, "_NET_WM_ICON_NAME", false);

	// Normally we would ask EGL which visual it would like, but MESA
	// actively refuses to return those compatible with transparent windows.
	// See: https://bugs.freedesktop.org/show_bug.cgi?id=67676
	// Instead, we will just grab one we like and hope it works!
	if (!XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &visual_info))
		die("failed to find compatible visual");

	attrs.background_pixel = 0;
	attrs.border_pixel = 0;
	attrs.event_mask = KeyPressMask|KeyReleaseMask|FocusChangeMask;
	attrs.colormap = XCreateColormap(display, DefaultRootWindow(display), visual_info.visual, AllocNone);

	window = XCreateWindow(display, DefaultRootWindow(display), 0, 0,
		window_width, window_height, 0, visual_info.depth, InputOutput,
		visual_info.visual,
		CWBackPixel|CWBorderPixel|CWEventMask|CWColormap, &attrs);

	if (!(normal_hints = XAllocSizeHints()))
		pdie("failed to allocate XSizeHints");

	if (!(hints = XAllocWMHints())) {
		XFree(normal_hints);
		pdie("failed to allocate XWMHints");
	}

	if (!(class_hint = XAllocClassHint())) {
		XFree(normal_hints);
		XFree(hints);
		pdie("failed to allocate XClassHint");
	}

	normal_hints->flags = PMinSize|PMaxSize;
	normal_hints->min_width = window_width;
	normal_hints->min_height = window_height;
	normal_hints->max_width = window_width;
	normal_hints->max_height = window_height;

	hints->flags = InputHint|StateHint;
	hints->input = true;
	hints->initial_state = NormalState;

	class_hint->res_name = instance_name;
	class_hint->res_class = "Terminix";

	XStoreName(display, window, "Terminix");
	XSetIconName(display, window, "Terminix");
	XSetWMNormalHints(display, window, normal_hints);
	XSetWMHints(display, window, hints);
	XSetClassHint(display, window, class_hint);
	XSetWMProtocols(display, window, &wm_delete_window, 1);
	XMapWindow(display, window);

	XFree(hints);
	XFree(normal_hints);
	XFree(class_hint);
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

// TODO : XRegisterIMInstantiateCallback, XOpenIM's optional parameters
static void
init_xim()
{
	if (!XSetLocaleModifiers(""))
		warnx("failed to set Xlib's locale modifiers");

	if (!(xim = XOpenIM(display, NULL, NULL, NULL)))
		die("failed to open X Input Method");

	if (!(xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing|XIMStatusNothing, XNClientWindow, window, NULL)))
		die("failed to create input method context");
}

static void
handle_key(XKeyEvent *event)
{
	char buffer[32];
	int bufsize;
	KeySym keysym;
	Status status;

	bufsize = Xutf8LookupString(xic, event, buffer, sizeof(buffer) - 1, &keysym, &status);

	if (status == XBufferOverflow) {
		warnx("buffer overflow in Xutf8LookupString");
		return;
	}

	if (status == XLookupNone || mode[TRANSMIT_DISABLED] || (!mode[DECARM] && keystate[event->keycode]))
		return;

	if (status == XLookupKeySym || status == XLookupBoth) {
		switch (keysym) {
		case XK_Pause:
			if (event->state & ShiftMask)
				warnx("TODO : transmit answerback");
			else
				ptwrite((mode[PAUSED] = !mode[PAUSED]) ? "\x13" : "\x11");
			return;
		case XK_Break: ptbreak(event->state & ShiftMask); break;
		case XK_Print: warnx("TODO : print screen"); break;
		case XK_Menu: warnx("TODO : SETUP"); break;
		case XK_Home: ptwrite("\33[1~"); return;
		case XK_Insert: ptwrite("\33[2~"); return;
		case XK_End: ptwrite("\33[4~"); return;
		case XK_Page_Up: ptwrite("\33[5~"); return;
		case XK_Page_Down: ptwrite("\33[6~"); return;
		case XK_F1: ptwrite(mode[DECANM] ? "\33OP" : "\33P"); return;
		case XK_F2: ptwrite(mode[DECANM] ? "\33OQ" : "\33Q"); return;
		case XK_F3: ptwrite(mode[DECANM] ? "\33OR" : "\33R"); return;
		case XK_F4: ptwrite(mode[DECANM] ? "\33OS" : "\33S"); return;
		// TODO : the rest of the function keys
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

		if (mode[DECKPAM])
			switch (keysym) {
			case XK_KP_0: kpam('p'); return;
			case XK_KP_1: kpam('q'); return;
			case XK_KP_2: kpam('r'); return;
			case XK_KP_3: kpam('s'); return;
			case XK_KP_4: kpam('t'); return;
			case XK_KP_5: kpam('u'); return;
			case XK_KP_6: kpam('v'); return;
			case XK_KP_7: kpam('w'); return;
			case XK_KP_8: kpam('x'); return;
			case XK_KP_9: kpam('y'); return;
			case XK_KP_Subtract: kpam('m'); return;
			case XK_KP_Separator: kpam('l'); return;
			case XK_KP_Decimal: kpam('n'); return;
			}
	}

	if (status == XLookupChars || status == XLookupBoth) {
		buffer[bufsize] = 0;

		if (bufsize == 1 && buffer[0] == '\r') {
			if (event->state & ShiftMask)
				ptwrite("\n");
			else
				ptwrite(mode[LNM] ? "\r\n" : "\r");
		} else {
			ptwrite("%s", buffer);
		}
	}
}

static void
kpam(char c)
{
	ptwrite("\33%c%c", mode[DECANM] ? 'O' : '?', c);
}