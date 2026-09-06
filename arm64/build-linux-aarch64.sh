#!/usr/bin/env bash
# Build Ring Out for Linux aarch64.
#
# On x86_64: native-speed cross compile (aarch64-linux-gnu-g++ + Debian 12
# arm64 sysroot). On aarch64: native gcc.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${REPO:-$(cd "$ROOT/.." && pwd)}"
OUT="${OUT:-$REPO/release}"
JOBS="${JOBS:-6}"
VERSION="${VERSION:-$(cat "$REPO/VERSION" 2>/dev/null || echo 1.5.2)}"
CROSS_IMAGE="${CROSS_IMAGE:-ringout-arm64-cross}"

uname_m="$(uname -m)"
if [ "${RINGOUT_IN_CROSS:-0}" != 1 ] && [ "$uname_m" != "aarch64" ] && [ "$uname_m" != "arm64" ]; then
  echo "==> host is $uname_m; cross-compiling with $CROSS_IMAGE"
  export TMPDIR="${TMPDIR:-$ROOT/tmp}"
  mkdir -p "$TMPDIR" "$ROOT/ccache" "$OUT"
  test -d "$ROOT/sysroot/usr/include" || { echo "missing sysroot at $ROOT/sysroot" >&2; exit 1; }
  docker build -t "$CROSS_IMAGE" -f "$ROOT/docker/aarch64-cross.Dockerfile" "$ROOT/docker"
  exec docker run --rm \
    -e TMPDIR=/work/tmp \
    -e CCACHE_DIR=/work/ccache \
    -e JOBS="$JOBS" \
    -e VERSION="$VERSION" \
    -e REPO=/src \
    -e BUILD=/work/build-arm64-cross \
    -e OUT=/work/release \
    -e RINGOUT_IN_CROSS=1 \
    -v "$ROOT:/work" \
    -v "$REPO:/src" \
    -w /src \
    "$CROSS_IMAGE" \
    bash /work/build-linux-aarch64.sh
fi

BUILD="${BUILD:-$ROOT/build-arm64}"

echo "=== Ring Out Linux aarch64 ==="
echo "    host:    $(uname -a)"
echo "    repo:    $REPO"
echo "    build:   $BUILD"
echo "    out:     $OUT"
echo "    jobs:    $JOBS"
echo "    version: $VERSION"
if [ "${RINGOUT_IN_CROSS:-0}" = 1 ]; then
  echo "    gcc:     $(aarch64-linux-gnu-gcc -dumpmachine) $(aarch64-linux-gnu-gcc -dumpfullversion 2>/dev/null || aarch64-linux-gnu-gcc -dumpversion)"
else
  echo "    gcc:     $(gcc -dumpmachine) $(gcc -dumpfullversion 2>/dev/null || gcc -dumpversion)"
fi
echo "    cmake:   $(cmake --version | head -1)"

git config --global --add safe.directory "$REPO" 2>/dev/null || true
git config --global --add safe.directory /src 2>/dev/null || true

test -f "$REPO/ModernGekko/CMakeLists.txt"
test -f "$REPO/DolRecomp/CMakeLists.txt"

mkdir -p "$BUILD" "$OUT" "${CCACHE_DIR:-$ROOT/ccache}"
LAUNCH=""
if command -v ccache >/dev/null 2>&1; then
  LAUNCH="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
  ccache -s || true
fi

echo "==> configure"
TOOLCHAIN=()
if [ "${RINGOUT_IN_CROSS:-0}" = 1 ]; then
  TOOLCHAIN=(-DCMAKE_TOOLCHAIN_FILE=/work/cmake/aarch64-linux-gnu.cmake)
  export PKG_CONFIG_SYSROOT_DIR=/work/sysroot
  export PKG_CONFIG_LIBDIR=/work/sysroot/usr/lib/aarch64-linux-gnu/pkgconfig:/work/sysroot/usr/share/pkgconfig
  export PKG_CONFIG_PATH=
fi
if [ -f "$BUILD/build.ninja" ] && [ "${FORCE_RECONFIGURE:-0}" != 1 ]; then
  echo "    reusing existing $BUILD/build.ninja"
else
  cmake -S "$REPO/ModernGekko" -B "$BUILD" -GNinja \
    "${TOOLCHAIN[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_QT=OFF \
    -DENABLE_TESTS=OFF \
    -DENABLE_ANALYTICS=OFF \
    -DENABLE_AUTOUPDATE=OFF \
    -DBUILD_TESTING=OFF \
    $LAUNCH
fi

echo "==> build moderngekko-run + dolrecomp"
cmake --build "$BUILD" --target moderngekko-run dolrecomp -j"$JOBS"

RUNTIME="$BUILD/moderngekko-run"
DOLRECOMP="$BUILD/dolrecomp-build/dolrecomp"
if [ ! -x "$DOLRECOMP" ]; then
  DOLRECOMP="$(find "$BUILD" -name dolrecomp -type f -executable | head -1)"
fi
test -x "$RUNTIME"
test -x "$DOLRECOMP"

echo "==> artifacts"
file "$RUNTIME" "$DOLRECOMP"
ls -lh "$RUNTIME" "$DOLRECOMP"

for bin in "$RUNTIME" "$DOLRECOMP"; do
  desc="$(file -b "$bin")"
  case "$desc" in
    *aarch64*) ;;
    *)
      echo "FAIL: $bin is not ARM aarch64: $desc" >&2
      exit 1
      ;;
  esac
done

STAGE="$OUT/RingOut-$VERSION-linux-aarch64"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/tools" "$STAGE/shaders" "$STAGE/lib" "$STAGE/source"

SRC="$REPO/dist/RingOut-1.0-dist"
install -m 755 "$SRC/setup.sh"   "$STAGE/setup.sh"
install -m 755 "$SRC/RingOut"    "$STAGE/RingOut"
install -m 644 "$SRC/README.txt" "$STAGE/README.txt"
install -m 644 "$SRC/CREDITS.txt" "$STAGE/CREDITS.txt"
sed -i "1s/^Ring Out - Ver .*/Ring Out - Ver $VERSION-aarch64/" "$STAGE/README.txt"
sed -i "1s/^Ring Out - Ver .*/Ring Out - Ver $VERSION-aarch64/" "$STAGE/CREDITS.txt"

install -m 755 "$RUNTIME" "$STAGE/bin/moderngekko-run"
install -m 755 "$DOLRECOMP" "$STAGE/tools/dolrecomp"
install -m 755 "$REPO/dist/shared/gc-art.py" "$STAGE/tools/gc-art.py"
if [ -f "$REPO/.github/input-scripts/arcade-match.txt" ]; then
  install -m 644 "$REPO/.github/input-scripts/arcade-match.txt" "$STAGE/tools/train-route.txt"
fi
for f in "$SRC"/shaders/*.glsl; do
  [ -e "$f" ] && install -m 644 "$f" "$STAGE/shaders/"
done
cp -a "$SRC/module-src" "$STAGE/module-src"

if [ -d "$BUILD/Sys" ]; then
  cp -a "$BUILD/Sys" "$STAGE/bin/Sys"
elif [ -d "$REPO/ModernGekko/vendor/dolphin/Data/Sys" ]; then
  cp -a "$REPO/ModernGekko/vendor/dolphin/Data/Sys" "$STAGE/bin/Sys"
fi

if [ -x "$REPO/dist/shared/stage-gamesettings.sh" ]; then
  "$REPO/dist/shared/stage-gamesettings.sh" "$STAGE" || true
fi

echo "==> collecting aarch64 support libraries"
collect_libs() {
  local bin="$1"
  local dest="$2"
  local sys="${PKG_CONFIG_SYSROOT_DIR:-/}"
  local dump
  dump="$(command -v aarch64-linux-gnu-objdump || command -v objdump)"
  python3 - "$bin" "$dest" "$sys" "$dump" <<'PY'
import os, subprocess, sys
bin_path, dest, sysroot, dump = sys.argv[1:5]
skip = {"libc.so.6", "libm.so.6", "libpthread.so.0", "ld-linux-aarch64.so.1", "linux-vdso.so.1"}
search = [
    os.path.join(sysroot, "usr/lib/aarch64-linux-gnu"),
    os.path.join(sysroot, "lib/aarch64-linux-gnu"),
    "/usr/lib/aarch64-linux-gnu",
    "/lib/aarch64-linux-gnu",
]
seen, queue = set(), [bin_path]
os.makedirs(dest, exist_ok=True)

def needed(elf):
    out = subprocess.check_output([dump, "-p", elf], text=True, errors="replace")
    names = []
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("NEEDED"):
            names.append(line.split()[-1])
    return names

def locate(name):
    for d in search:
        p = os.path.join(d, name)
        if os.path.isfile(p) or os.path.islink(p):
            return p
    return None

while queue:
    cur = queue.pop()
    try:
        names = needed(cur)
    except subprocess.CalledProcessError:
        continue
    for name in names:
        if name in skip or name in seen:
            continue
        seen.add(name)
        src = locate(name)
        if not src:
            print(f"    missing {name}", flush=True)
            continue
        dst = os.path.join(dest, name)
        try:
            subprocess.check_call(["cp", "-L", src, dst])
        except subprocess.CalledProcessError:
            continue
        queue.append(dst)
print(len([n for n in seen if n not in skip]))
PY
}
collect_libs "$RUNTIME" "$STAGE/lib"
echo "    $(ls "$STAGE/lib" | wc -l) libraries"

# GPL source shipment: keep a pointer, not a second 500 MB tree.
cat > "$STAGE/source/README.txt" <<EOF
Ring Out $VERSION aarch64
Runtime and recompiler were built from the RingOut tree plus the patches in
this package's ARM64_INCOMPLETE.txt / port notes.
Full source: https://github.com/jackpoison-prog/RingOut
EOF

cat > "$STAGE/ARM64_INCOMPLETE.txt" <<EOF
Ring Out $VERSION — Linux aarch64 port
======================================

This package is a native aarch64 rebuild. It is NOT the official x86_64 zip
renamed. Prebuilt host binaries (moderngekko-run, dolrecomp, bundled .so)
were compiled for ARM aarch64 against Debian 12 (glibc 2.36).

NOT INCLUDED (same as the official desktop zip, plus one ARM-specific note):
  - Any GameCube disc / extracted game/
  - bin/g*_recomp.so  (must be compiled on this machine from YOUR disc)
  - x86_64 PGO profiles are shipped but setup.sh will ignore them on aarch64
  - libc-fallback/ld-linux-x86-64.so.2 (x86_64-only)

TO FINISH:
  1. Install cmake ninja-build clang python3 on the aarch64 machine
  2. Confirm a Vulkan ICD:  vulkaninfo | head
  3. ./setup.sh /path/to/your-disc.iso
  4. Optional, +10-14% CPU:  ./setup.sh --pgo /path/to/your-disc.iso
     Do NOT pass --deck (that flag is Steam Deck x86_64).

GPU: Vulkan first, then  ./RingOut -v OGL  if needed.
EOF

# Prepend a short ARM64 note to the player README.
python3 - "$STAGE/README.txt" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
text = p.read_text()
note = """
LINUX AARCH64
  This is a native ARM64 build. Do not use the x86_64 zip under box64.
  --deck is Steam Deck (x86_64) only. Use  ./setup.sh /path/to/disc.iso
  Shipped PGO profiles are x86_64; train with --pgo on this machine.
  See ARM64_INCOMPLETE.txt.

"""
if "LINUX AARCH64" not in text:
    # Insert after QUICK START header block's first paragraph-ish: after line 3
    lines = text.splitlines(True)
    p.write_text("".join(lines[:6]) + note + "".join(lines[6:]))
PY

ZIP="$OUT/RingOut-$VERSION-linux-aarch64.zip"
( cd "$OUT" && rm -f "$ZIP" && zip -r -q "$ZIP" "RingOut-$VERSION-linux-aarch64" )
echo "==> packed $ZIP"
ls -lh "$ZIP"
echo "==> file(1) of payload"
file "$STAGE/bin/moderngekko-run" "$STAGE/tools/dolrecomp"
echo "==> ccache"
ccache -s || true
echo "DONE"
