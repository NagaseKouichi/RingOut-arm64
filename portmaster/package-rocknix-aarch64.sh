#!/usr/bin/env bash
# Create a direct ROCKNIX / PortMaster package from the generic ARM64 runtime.
#
# Output: release/RingOut-<version>-rocknix-aarch64.zip
# The archive deliberately excludes game data and game-derived modules.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(cat "$ROOT/VERSION")"
SOURCE_ZIP="${SOURCE_ZIP:-$ROOT/../release/RingOut-$VERSION-armada-aarch64.zip}"
OUT="${OUT:-$ROOT/../release}"
WORK="$OUT/_rocknix-portmaster-stage"
SOURCE="$WORK/_source/RingOut-$VERSION-armada"
PORT="$WORK/RingOut"

[ -f "$SOURCE_ZIP" ] || { echo "missing ARM64 runtime zip: $SOURCE_ZIP" >&2; exit 1; }

rm -rf "$WORK"
mkdir -p "$PORT" "$WORK/_source"
python3 - "$SOURCE_ZIP" "$WORK/_source" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    z.extractall(sys.argv[2])
PY

[ -f "$SOURCE/RingOut" ] || { echo "missing ARM64 runtime launcher: $SOURCE/RingOut" >&2; exit 1; }
[ -f "$SOURCE/bin/moderngekko-run" ] || { echo "missing ARM64 runtime: $SOURCE/bin/moderngekko-run" >&2; exit 1; }

# Python's zip extractor intentionally does not restore Unix modes. The source
# archive stores them, but set the two process entry points explicitly before
# staging so this packer is portable across Python versions.
#
# PortMaster puts RingOut.sh directly in /storage/roms/ports and its payload in
# the neighbouring RingOut/ directory.
install -m 755 "$ROOT/portmaster/RingOut.sh" "$WORK/RingOut.sh"
cp -a "$SOURCE/bin" "$SOURCE/lib" "$SOURCE/shaders" "$SOURCE/tools" "$PORT/"
[ ! -d "$SOURCE/gamesettings" ] || cp -a "$SOURCE/gamesettings" "$PORT/"
install -m 755 "$SOURCE/RingOut" "$PORT/RingOut"
chmod 755 "$PORT/bin/moderngekko-run"
install -m 644 "$SOURCE/CREDITS.txt" "$PORT/CREDITS.txt"
install -m 644 "$ROOT/portmaster/README-ROCKNIX.txt" "$PORT/README.txt"

# Keep mount points and user-visible paths explicit, but empty. They must be
# populated locally from the player's own disc and are checked below.
mkdir -p "$PORT/game" "$PORT/userdata"

python3 - "$WORK" "$VERSION" <<'PY'
from pathlib import Path
import stat, sys
root = Path(sys.argv[1])
version = sys.argv[2]
for p in (root / 'RingOut.sh', root / 'RingOut' / 'RingOut', root / 'RingOut' / 'bin' / 'moderngekko-run'):
    if not p.is_file() or not (p.stat().st_mode & stat.S_IXUSR):
        raise SystemExit(f'not executable: {p}')
for path in root.rglob('*'):
    n = path.name
    if n.endswith('_recomp.so') or n.endswith(('.rvz', '.iso', '.gcm', '.wbfs', '.gcz', '.wia')):
        raise SystemExit(f'forbidden disc-derived file: {path}')
if any((root / 'RingOut' / 'game').iterdir()):
    raise SystemExit('game directory must be empty in the public package')
print('payload validation: OK')
PY

ZIP="$OUT/RingOut-$VERSION-rocknix-aarch64.zip"
rm -f "$ZIP"
( cd "$WORK" && TZ=UTC zip -X -qr "$ZIP" RingOut.sh RingOut )

python3 - "$ZIP" <<'PY'
import stat, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    names = z.namelist()
    required = {'RingOut.sh', 'RingOut/RingOut', 'RingOut/bin/moderngekko-run', 'RingOut/README.txt'}
    missing = required.difference(names)
    if missing:
        raise SystemExit(f'missing: {sorted(missing)}')
    forbidden = [n for n in names if n.endswith('_recomp.so') or n.endswith(('.rvz', '.iso', '.gcm', '.wbfs', '.gcz', '.wia'))]
    if forbidden:
        raise SystemExit(f'forbidden: {forbidden}')
    for name in ('RingOut.sh', 'RingOut/RingOut', 'RingOut/bin/moderngekko-run'):
        if not ((z.getinfo(name).external_attr >> 16) & stat.S_IXUSR):
            raise SystemExit(f'not executable in zip: {name}')
    print(f'{sys.argv[1]}: {len(names)} members; validation OK')
PY
ls -lh "$ZIP"
