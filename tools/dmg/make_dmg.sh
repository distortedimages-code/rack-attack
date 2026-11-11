#!/bin/bash
# Builds the installer disk image (macOS only): the three bundles and, next to each, a
# shortcut to the folder it goes in.
#   tools/dmg/make_dmg.sh <folder holding the three bundles> <output .dmg>
# Needs dmgbuild:  python3 -m pip install dmgbuild==1.6.7
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$1" && pwd)"
OUT="$2"

for b in "RackAttack Editor.component" "RackAttack Editor.vst3" "RackAttack Editor.app"; do
    [ -d "$SRC/$b" ] || { echo "missing: $SRC/$b"; exit 1; }
done

rm -f "$OUT"
python3 -m dmgbuild -s "$HERE/dmg_settings.py" -D src="$SRC" "RackAttack Editor" "$OUT"
hdiutil verify "$OUT"
echo "built $OUT"
