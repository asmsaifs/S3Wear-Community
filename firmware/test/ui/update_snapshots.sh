#!/usr/bin/env bash
# Re-render every UI snapshot golden (firmware/test/ui_snapshots/<name>.png) from
# its scenario (firmware/test/ui/<name>.txt). Review the PNG diffs before committing.
# --community: the Community edition's (test/ui/community, ui_snapshots/community) with the
# simulator in build/sim-community (cmake -S firmware/simulator -B build/sim-community -DS3W_EDITION_PRO=OFF).
#
#   firmware/test/ui/update_snapshots.sh [--community] [name...]   # default: all scenarios
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="$ROOT/build/sim"
SUB=""
if [ "${1:-}" = "--community" ]; then
    shift
    BUILD="$ROOT/build/sim-community"
    SUB="/community"
fi
SIM="$BUILD/s3w_sim"
IN="$ROOT/firmware/test/ui$SUB"
OUT="$ROOT/firmware/test/ui_snapshots$SUB"
cmake --build "$BUILD" >/dev/null
mkdir -p "$OUT"

if [ $# -eq 0 ]; then
    set -- $(cd "$IN" && ls *.txt | sed 's/\.txt$//')
fi
for name in "$@"; do
    "$SIM" --script "$IN/$name.txt" --screenshot "$OUT/$name.png" >/dev/null
    echo "updated $name.png"
done
