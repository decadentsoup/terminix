SHELL	= /bin/sh
CC	= gcc
SOURCES	= src/opengl.c src/ptmx.c src/screen.c src/terminix.c src/unifont.c \
	src/vt52.c src/vt100.c src/vtinterp.c src/xlib.c

.PHONY: all clean

all: terminix

terminix: $(SOURCES)
	$(CC) \
		-DPKGVER="\"r`git rev-list --count HEAD`.`git rev-parse --short HEAD`\"" \
		-Werror -Wall -Wextra \
		$(SOURCES) \
		-o terminix \
		-lX11 -lEGL -lGLESv2

src/unifont.c: buildfont.rb
	./buildfont.rb

clean:
	rm -f terminix src/unifont.c