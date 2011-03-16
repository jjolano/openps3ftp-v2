#!/bin/sh -e
# simply designed to clean up what build.sh left

# go to sh script directory
cd "$(dirname "$0")"

OUTDIR="output"

echo "[make] Cleaning..."
make clean > /dev/null

echo "[rm] Removing output directories..."
rm -fr "$OUTDIR"

echo "done"
# end
