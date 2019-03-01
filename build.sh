#!/bin/sh -e
cd "$(dirname "$0")"
gcc -Werror -Wall -Wextra src/*.c -o terminix -lGL -lGLU -lGLEW -lglfw