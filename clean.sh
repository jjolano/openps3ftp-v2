#!/bin/sh -e
# simply designed to clean up what build.sh left

# go to sh script directory
cd "$(dirname "$0")"

OUTDIR="output"

echo -n "[rm] $OUTDIR..."
rm -fr "$OUTDIR"
echo "OK"

echo -n "[make] clean..."
make clean > /dev/null
echo "OK"

# end
