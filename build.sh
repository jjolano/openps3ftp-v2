#!/bin/sh -e
# this will build, hash, and zip OpenPS3FTP (nopass and normal versions).

# go to sh script directory
cd "$(dirname "$0")"

OUTDIR="output"

PKGFILE="$(basename $(pwd))"
ZIPFILE="$OUTDIR/$PKGFILE.zip"

SEDFILE="./include/common.h"

# create directories
mkdir -p "$OUTDIR"

# compile stuff and zip
## create 'nopass' version
echo -n "[make] nopass..."
sed -i 's/DISABLE_PASS\t[01]/DISABLE_PASS\t1/' "$SEDFILE"
make pkg > /dev/null
echo "OK"

mv "$PKGFILE.pkg" "$PKGFILE-nopass.pkg"
mv "$PKGFILE.geohot.pkg" "$PKGFILE-nopass.geohot.pkg"

## create 'normal' version
echo -n "[make] normal..."
sed -i 's/DISABLE_PASS\t[01]/DISABLE_PASS\t0/' "$SEDFILE"
make pkg > /dev/null
echo "OK"

## create zip
echo -n "[zip] $ZIPFILE..."
touch README COPYING changelog *.pkg
zip -q "$ZIPFILE" README COPYING changelog *.pkg
echo "OK"
# end
