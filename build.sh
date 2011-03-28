#!/bin/sh -e
# this will build, hash, and zip OpenPS3FTP (nopass and normal versions).

# go to sh script directory
cd "$(dirname "$0")"

command -v gmake >/dev/null 2>&1 && MAKE=gmake || MAKE=make

OUTDIR="output"

PKGFILE="$(basename $(pwd))"
ZIPFILE="$OUTDIR/$PKGFILE.zip"

# create directories
mkdir -p "$OUTDIR"

# compile stuff and zip
## create 'nopass' version
echo -n "[make] nopass..."
$MAKE DISABLE_PASS=1 clean pkg > /dev/null
echo "OK"

mv "$PKGFILE.pkg" "$PKGFILE-nopass.pkg"
mv "$PKGFILE.geohot.pkg" "$PKGFILE-nopass.geohot.pkg"

## create 'normal' version
echo -n "[make] normal..."
$MAKE DISABLE_PASS=0 clean pkg > /dev/null
echo "OK"

## create zip
echo -n "[zip] $ZIPFILE..."
touch README COPYING ChangeLog *.pkg
zip -q "$ZIPFILE" README COPYING ChangeLog *.pkg
echo "OK"
# end
