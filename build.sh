#!/bin/sh -e
command -v gmake >/dev/null 2>&1 && MAKE=gmake || MAKE=make

$MAKE -C src clean pkg
mv src/*.pkg .
$MAKE --no-print-directory -C src clean
zip -rl9 out README COPYING ChangeLog TODO *.pkg src

