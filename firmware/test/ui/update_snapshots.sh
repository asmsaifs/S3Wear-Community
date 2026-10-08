#!/usr/bin/env bash
# Re-render every UI snapshot golden (firmware/test/ui_snapshots/<name>.png) from
# its scenario (firmware/test/ui/<name>.txt). Review the PNG diffs before committing.
#
#   firmware/test/ui/update_snapshots.sh [name...]   # default: all scenarios
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SIM="$ROOT/build/sim/s3w_sim"
OUT="$ROOT/firmware/test/ui_snapshots"
cmake --build "$ROOT/build/sim" >/dev/null
mkdir -p "$OUT"

if [ $# -eq 0 ]; then
    set -- $(cd "$ROOT/firmware/test/ui" && ls *.txt | sed 's/\.txt$//')
fi
for name in "$@"; do
    "$SIM" --script "$ROOT/firmware/test/ui/$name.txt" --screenshot "$OUT/$name.png" >/dev/null
    echo "updated $name.png"
done
