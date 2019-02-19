#!/bin/sh -e

cd "$(dirname "$0")"

gcc -Werror -Wall -Wextra src/*.c -o terminix -lallegro -lallegro_image

if [ ! -e unifont.png ]; then
	echo "Missing unifont.png: see README.adoc"
fi