#!/bin/sh -e
cd "$(dirname "$0")"

command -v gmake >/dev/null 2>&1 && MAKE=gmake || MAKE=make

OUTDIR="output"

PKGFILE="$(basename $(pwd))"
ZIPFILE="$OUTDIR/$PKGFILE.zip"

echo "compiling ... $PKGFILE.pkg"
$MAKE clean pkg > /dev/null

echo "zipping ... $ZIPFILE"
mkdir -p "$OUTDIR"
touch README COPYING ChangeLog *.pkg
zip -q -l "$ZIPFILE" README COPYING ChangeLog *.pkg

$MAKE clean > /dev/null

