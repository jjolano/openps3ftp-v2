#!/bin/sh -e
cd "$(dirname "$0")"

command -v gmake >/dev/null 2>&1 && MAKE=gmake || MAKE=make

OUTDIR="output"

echo "cleaning ..."
rm -fr "$OUTDIR"

$MAKE clean > /dev/null

