#!/usr/bin/env bash
# Stage official-format aarch64 zips from the already-built runtime.
#
#   RingOut-$VERSION-linux-aarch64.zip     desktop compile package (setup.sh)
#   RingOut-$VERSION-armada-aarch64.zip    runtime-only, like steamdeck zip
#
# Allowlist only. Never copies game/, g*_recomp.so, work/, userdata dumps.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
VERSION="$(cat "$REPO/VERSION")"
BUILT="${BUILT:-$REPO/release/RingOut-$VERSION-linux-aarch64}"
OUT="${OUT:-$REPO/release}"
WORK="$OUT/_official-stage"
DIST="$REPO/dist/RingOut-1.0-dist"
DECK="$REPO/dist/RingOut-1.0-deck"

[ -x "$BUILT/bin/moderngekko-run" ] || { echo "missing runtime at $BUILT/bin/moderngekko-run" >&2; exit 1; }
[ -x "$BUILT/tools/dolrecomp" ] || { echo "missing dolrecomp" >&2; exit 1; }
[ -d "$BUILT/module-src" ] || { echo "missing module-src" >&2; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK"

assert_clean() {
  local stage="$1"
  local f
  for f in game work windows art bin/gGRSEAF_recomp.so userdata/Config userdata/GC userdata/Logs userdata/config.ini; do
    if [ -e "$stage/$f" ]; then
      echo "FAIL: $f is in $(basename "$stage")" >&2
      exit 1
    fi
  done
  if find "$stage" \( -name '*_recomp.so' -o -name '*.gci' -o -name '*.iso' -o -name '*.rvz' -o -name 'root.olk' \) | grep -q .; then
    echo "FAIL: disc-derived files in $(basename "$stage")" >&2
    find "$stage" \( -name '*_recomp.so' -o -name '*.gci' -o -name '*.iso' -o -name '*.rvz' -o -name 'root.olk' \)
    exit 1
  fi
  echo "  $(basename "$stage"): no disc-derived content"
}

copy_libs() {
  local dest="$1/lib"
  mkdir -p "$dest"
  cp -a "$BUILT/lib/." "$dest/"
  # pulse private helper lives in a subdir; ldd-style walker missed it
  local pulse="$ROOT/sysroot/usr/lib/aarch64-linux-gnu/pulseaudio/libpulsecommon-16.1.so"
  if [ -f "$pulse" ] && [ ! -e "$dest/libpulsecommon-16.1.so" ]; then
    cp -L "$pulse" "$dest/libpulsecommon-16.1.so"
  fi
}

# ---------------------------------------------------------------------------
# 1. Linux aarch64 compile package  (official linux-x86_64.zip shape)
# ---------------------------------------------------------------------------
COMPILE="$WORK/RingOut-$VERSION-linux-aarch64"
mkdir -p "$COMPILE/bin" "$COMPILE/tools" "$COMPILE/shaders"

install -m 755 "$BUILT/setup.sh"  "$COMPILE/setup.sh"
install -m 755 "$BUILT/RingOut"   "$COMPILE/RingOut"
install -m 644 "$DIST/README.txt" "$COMPILE/README.txt"
install -m 644 "$DIST/CREDITS.txt" "$COMPILE/CREDITS.txt"
sed -i "1s/^Ring Out - Ver .*/Ring Out - Ver $VERSION/" "$COMPILE/README.txt"
sed -i "1s/^Ring Out - Ver .*/Ring Out - Ver $VERSION/" "$COMPILE/CREDITS.txt"

python3 - "$COMPILE/README.txt" "$VERSION" <<'PY'
from pathlib import Path
import sys, re
p = Path(sys.argv[1])
ver = sys.argv[2]
text = p.read_text()
note = """
LINUX AARCH64
  Native ARM64 runtime and recompiler. Do not use the x86_64 zip under box64.
  --deck is Steam Deck (x86_64) only; omit it here.
  Shipped PGO profiles are x86_64; on aarch64 they are ignored. Train with
      ./setup.sh --pgo /path/to/your/disc.iso
  After setup, copy game/ and bin/g<ID>_recomp.so into the armada package.

"""
if "LINUX AARCH64" not in text:
    lines = text.splitlines(True)
    text = "".join(lines[:7]) + note + "".join(lines[7:])
text = text.replace("      ./setup.sh --deck /path/to/your/disc.iso    (module for a Steam Deck)\n", "")
armada = f"""ON ARMADA / AARCH64 RUNTIME PACKAGE?
  Download RingOut-{ver}-armada-aarch64.zip instead -- it ships the prebuilt
  runtime only (no setup.sh, no module-src). Build the module with THIS
  package, then copy game/ and bin/g<ID>_recomp.so across.

"""
text = re.sub(r"ON A STEAM DECK\?.*?(?=\nREQUIREMENTS\n)", armada, text, count=1, flags=re.S)
p.write_text(text)
PY

install -m 755 "$BUILT/bin/moderngekko-run" "$COMPILE/bin/moderngekko-run"
if [ -d "$BUILT/bin/Sys" ]; then
  cp -a "$BUILT/bin/Sys" "$COMPILE/bin/Sys"
fi
install -m 755 "$BUILT/tools/dolrecomp" "$COMPILE/tools/dolrecomp"
install -m 755 "$REPO/dist/shared/gc-art.py" "$COMPILE/tools/gc-art.py"
install -m 644 "$REPO/.github/input-scripts/arcade-match.txt" "$COMPILE/tools/train-route.txt"
for f in "$DIST"/shaders/*.glsl; do install -m 644 "$f" "$COMPILE/shaders/"; done
copy_libs "$COMPILE"
cp -a "$BUILT/module-src" "$COMPILE/module-src"

# Public binary releases carry the corresponding source shipment generated from
# this fork commit. Do not borrow the upstream x86 source folder: it would not
# describe these aarch64 binaries or their portability guards.
SRC_SHIP="${SRC_SHIP:-$REPO/release/gpl-source}"
if ! compgen -G "$SRC_SHIP/*.tar.gz" >/dev/null; then
  echo "FAIL: no GPL source shipment in $SRC_SHIP; run arm64/regen-source.sh first" >&2
  exit 1
fi
mkdir -p "$COMPILE/source"
cp -a "$SRC_SHIP/." "$COMPILE/source/"

"$REPO/dist/shared/stage-gamesettings.sh" "$REPO" "$COMPILE/userdata/GameSettings" || true

assert_clean "$COMPILE"
for bin in "$COMPILE/bin/moderngekko-run" "$COMPILE/tools/dolrecomp"; do
  file -b "$bin" | grep -q aarch64 || { echo "FAIL: $bin is not aarch64" >&2; exit 1; }
done
[ -x "$COMPILE/setup.sh" ]
[ -x "$COMPILE/RingOut" ]
[ ! -e "$COMPILE/bin/gGRSEAF_recomp.so" ]

ZIP1="$OUT/RingOut-$VERSION-linux-aarch64.zip"
rm -f "$ZIP1"
( cd "$WORK" && TZ=UTC zip -X -qr "$ZIP1" "$(basename "$COMPILE")" )

# ---------------------------------------------------------------------------
# 2. Armada runtime package  (official steamdeck zip shape)
# ---------------------------------------------------------------------------
ARMADA="$WORK/RingOut-$VERSION-armada"
mkdir -p "$ARMADA/bin" "$ARMADA/lib" "$ARMADA/shaders" "$ARMADA/tools"

install -m 755 "$BUILT/bin/moderngekko-run" "$ARMADA/bin/moderngekko-run"
if [ -d "$BUILT/bin/Sys" ]; then
  cp -a "$BUILT/bin/Sys" "$ARMADA/bin/Sys"
fi
# Deck-style launcher: no setup, requires game/ + module copied in.
install -m 755 "$DECK/RingOut" "$ARMADA/RingOut"
install -m 755 "$REPO/dist/shared/gc-art.py" "$ARMADA/tools/gc-art.py"
install -m 644 "$DIST/CREDITS.txt" "$ARMADA/CREDITS.txt"
sed -i "1s/^Ring Out - Ver .*/Ring Out - Ver $VERSION/" "$ARMADA/CREDITS.txt"
for f in "$DIST"/shaders/*.glsl; do install -m 644 "$f" "$ARMADA/shaders/"; done
copy_libs "$ARMADA"
"$REPO/dist/shared/stage-gamesettings.sh" "$REPO" "$ARMADA/gamesettings" || true

cat > "$ARMADA/README.txt" <<EOF
Ring Out - Ver $VERSION
Linux aarch64 runtime (armada)
==============================

This package contains NO game data and NO game code. There is no setup
step here: the runtime is prebuilt. You compile the module once on an
aarch64 Linux machine (or cross-compile), then copy two things across.

GETTING YOUR GAME ONTO IT
  1. On an aarch64 Linux machine, unpack RingOut-$VERSION-linux-aarch64.zip
     and run:
         ./setup.sh /path/to/your/disc.iso
     Do NOT pass --deck (that flag is Steam Deck x86_64).
  2. Copy its  game/  directory into this folder
  3. Copy its  bin/g<ID>_recomp.so  into this folder's  bin/

  The launcher checks for both and will tell you which one is missing.

  The module MUST be an ARM aarch64 .so. A module built from the official
  linux-x86_64 or steamdeck-x86_64 zip will not load.

INSTALLING
  Put this folder anywhere. Run ./RingOut from a terminal, or add it as
  a non-Steam / desktop shortcut.

  If your archive manager did not preserve executable permissions, run this
  once from the package directory before starting the game:
      chmod +x RingOut bin/moderngekko-run

  Linux `unzip` preserves these permissions; graphical file managers and some
  file-copy workflows may not.

REQUIREMENTS
  - A working Vulkan driver
  - glibc 2.38 or newer (Fedora aarch64 is fine)
  - game/ and bin/g<ID>_recomp.so produced from YOUR disc

CONTROLS
  Escape          settings menu
  Arrow keys      navigate; Left/Right change a value or switch tab
  Space           confirm / activate
  Alt+W           toggle widescreen (16:9)
  Alt+Enter       fullscreen
  F1-F8           load state      Shift+F1-F8   save state
  Shift+Escape    quit

  Override the video backend:  ./RingOut -v OGL   or   ./RingOut -v Vulkan

WHAT IS IN THIS FOLDER
  RingOut           launcher (no compiler, no setup)
  bin/              the runtime
  lib/              bundled support libraries
  shaders/          optional post-process filters
  gamesettings/     cheat lists (installed into userdata on first run)
  tools/gc-art.py   extracts banner/icon from YOUR disc at run time

  Not shipped (copyright / derived from the disc):
  game/             your extracted disc -- copy from the compile package
  bin/g*_recomp.so  your recompiled module -- copy from the compile package

CREDITS, DISCLAIMER AND LICENSING
  See CREDITS.txt.
EOF

assert_clean "$ARMADA"
[ ! -e "$ARMADA/setup.sh" ]
[ ! -e "$ARMADA/module-src" ]
[ ! -e "$ARMADA/tools/dolrecomp" ]
[ ! -e "$ARMADA/tools/train-route.txt" ]
file -b "$ARMADA/bin/moderngekko-run" | grep -q aarch64
[ -x "$ARMADA/RingOut" ]
[ -x "$ARMADA/bin/moderngekko-run" ]

ZIP2="$OUT/RingOut-$VERSION-armada-aarch64.zip"
rm -f "$ZIP2"
( cd "$WORK" && TZ=UTC zip -X -qr "$ZIP2" "$(basename "$ARMADA")" )

# Canary: zip members include the executable bit (Unix external attrs).
python3 - "$ZIP1" "$ZIP2" <<'PY'
import sys, zipfile, stat
for zpath in sys.argv[1:]:
    with zipfile.ZipFile(zpath) as z:
        names = z.namelist()
        assert not any('/game/' in n or n.endswith('_recomp.so') for n in names), zpath
        if 'linux-aarch64' in zpath:
            assert any('/source/' in n and n.endswith('.tar.gz') for n in names), zpath
        for want in ('RingOut', 'bin/moderngekko-run'):
            hits = [n for n in names if n.endswith('/'+want) or n.endswith(want)]
            assert hits, (zpath, want)
            info = z.getinfo(hits[0])
            mode = (info.external_attr >> 16) & 0o777
            assert mode & 0o111, (zpath, want, oct(mode))
        print(zpath, 'ok', len(names), 'members')
PY

echo
echo "==> official-format aarch64 zips"
ls -lh "$ZIP1" "$ZIP2"
echo
echo "-- compile package --"
( cd "$COMPILE" && du -sh -- * | sort -h )
echo
echo "-- armada package --"
( cd "$ARMADA" && du -sh -- * | sort -h )
