#!/bin/bash
# RackAttack Editor's Mac disk image: an installer (AU, VST3 and the app, each one a choice) and,
# beside it, the same three bundles with shortcuts to their folders for those who drag them by hand,
# and a read-me with the Terminal commands.
#   tools/dmg/make_dmg.sh <folder holding the three bundles> <output .dmg> <version>
# Needs macOS (pkgbuild, productbuild, hdiutil, tiffutil) and dmgbuild (pip install dmgbuild==1.6.7).
# The bundles are signed ad hoc (no Apple developer certificate): macOS may refuse to open the
# installer at first; the read-me and the web page give the Terminal command that installs it anyway.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$1" && pwd)"
OUT="$2"
VER="$3"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for b in "RackAttack Editor.component" "RackAttack Editor.vst3" "RackAttack Editor.app"; do
    [ -d "$SRC/$b" ] || { echo "missing: $SRC/$b"; exit 1; }
done

mkdir -p "$WORK/root/au" "$WORK/root/vst3" "$WORK/root/app" "$WORK/pkgs" "$WORK/stage" "$WORK/res"
ditto "$SRC/RackAttack Editor.component" "$WORK/root/au/RackAttack Editor.component"
ditto "$SRC/RackAttack Editor.vst3"      "$WORK/root/vst3/RackAttack Editor.vst3"
ditto "$SRC/RackAttack Editor.app"       "$WORK/root/app/RackAttack Editor.app"

# one package per bundle, none relocatable (an older copy elsewhere on the disk must not catch the install)
pkg () {   # $1 root, $2 install location, [$3 scripts]
    pkgbuild --analyze --root "$WORK/root/$1" "$WORK/$1.plist" > /dev/null
    python3 -c 'import plistlib, sys
p = sys.argv[1]; d = plistlib.load(open(p, "rb"))
for e in d: e["BundleIsRelocatable"] = False
plistlib.dump(d, open(p, "wb"))' "$WORK/$1.plist"
    pkgbuild --root "$WORK/root/$1" --component-plist "$WORK/$1.plist" --identifier "com.distortedimages.rackattackeditor.$1" \
        --version "$VER" --install-location "$2" ${3:+--scripts "$3"} "$WORK/pkgs/$1.pkg"
}
pkg au   "/Library/Audio/Plug-Ins/Components" "$HERE/scripts-au"
pkg vst3 "/Library/Audio/Plug-Ins/VST3"       "$HERE/scripts-vst3"
pkg app  "/Applications"

sed "s/@VERSION@/$VER/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
for f in welcome.html conclusion.html; do sed "s/@VERSION@/$VER/g" "$HERE/resources/$f" > "$WORK/res/$f"; done
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/res" --package-path "$WORK/pkgs" \
    "$WORK/stage/Installer RackAttack Editor.pkg"

# the disk image: the installer, the bundles (the same ones), the read-me; shortcuts and window in dmg_settings.py
ditto "$SRC/RackAttack Editor.component" "$WORK/stage/RackAttack Editor.component"
ditto "$SRC/RackAttack Editor.vst3"      "$WORK/stage/RackAttack Editor.vst3"
ditto "$SRC/RackAttack Editor.app"       "$WORK/stage/RackAttack Editor.app"
python3 -c 'import sys
t = open(sys.argv[1], encoding="utf-8").read()
t = t.replace("@COMMANDE@", open(sys.argv[2], encoding="utf-8").read().strip())
t = t.replace("@INSTALLEUR@", open(sys.argv[3], encoding="utf-8").read().strip())
open(sys.argv[4], "w", encoding="utf-8").write(t)' \
    "$HERE/LISEZ-MOI.txt" "$HERE/terminal-command.txt" "$HERE/installer-command.txt" "$WORK/stage/LISEZ-MOI.txt"
grep -q "auval -v aumu" "$WORK/stage/LISEZ-MOI.txt"
tiffutil -cathidpicheck "$HERE/background.png" "$HERE/background@2x.png" -out "$WORK/background.tiff"

rm -f "$OUT"
python3 -m dmgbuild -s "$HERE/dmg_settings.py" -D stage="$WORK/stage" -D bg="$WORK/background.tiff" "RackAttack Editor" "$OUT"
hdiutil verify "$OUT"
echo "built $OUT"
