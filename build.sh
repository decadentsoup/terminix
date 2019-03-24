#!/bin/sh -e
cd "$(dirname "$0")"
version="r$(git rev-list --count HEAD).$(git rev-parse --short HEAD)"
gcc \
	-DPKGVER="\"$version\"" -Werror -Wall -Wextra src/*.c -o terminix \
	-lX11 -lEGL -lGLESv2