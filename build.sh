#!/bin/sh -e

cd "$(dirname "$0")"

gcc -Werror -Wall -Wextra src/*.c -o terminix -lallegro -lallegro_font \
	-lallegro_ttf

ftp="https://ftp.gnu.org/gnu/unifont/unifont-11.0.03"
ftpmirror="https://ftpmirror.gnu.org/gnu/unifont/unifont-11.0.03"

warnfont() {
	if [ ! -e "$1" ]; then
		echo "Missing $1: download it from"
		echo "  $ftp/$2"
		echo "  -or-"
		echo "  $ftpmirror/$2"
		echo "and name it $1 in the same directory as build.sh"
	fi
}

warnfont unifont-bmp.ttf unifont-11.0.03.ttf
warnfont unifont-smp.ttf unifont_upper-11.0.03.ttf
warnfont unifont-csur.ttf unifont_csur-11.0.03.ttf