#!/usr/bin/env bash
# GPL corresponding-source shipment for the aarch64 desktop zip.
#
# Nested git checkouts are disabled in this tree (vendored as plain files),
# so this archives the trees as they exist in the parent RingOut-arm64 commit
# rather than calling git archive inside ModernGekko / Dolphin / DolRecomp.
#
#   ./arm64/regen-source.sh
#   DEST=... ./arm64/regen-source.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
DEST="${DEST:-$REPO/release/gpl-source}"
SHORT="$(git -C "$REPO" rev-parse --short=12 HEAD)"
FULL="$(git -C "$REPO" rev-parse HEAD)"

mkdir -p "$DEST"
rm -f "$DEST"/*.tar.gz "$DEST"/*.patch "$DEST"/README.txt "$DEST"/MANIFEST.txt

archive_tree() {
  local name="$1"
  local tree="$2"
  local out="$DEST/${name}-${SHORT}.tar.gz"
  echo "  $name  HEAD:$tree"
  git -C "$REPO" archive --format=tar.gz --prefix="${name}-${SHORT}/" "HEAD:$tree" > "$out"
  ls -lh "$out" | awk '{print "    "$5"  "$9}'
}

echo "==> source shipment from $FULL"
# ModernGekko vendors Dolphin; archive it separately so the chassis tarball
# does not duplicate ~the whole Sys/Themes tree.
echo "  ModernGekko  HEAD:ModernGekko (without vendor/dolphin)"
git -C "$REPO" ls-files -z ModernGekko \
  | grep -z -v '^ModernGekko/vendor/dolphin/' \
  | tar --null -C "$REPO" --transform "s|^ModernGekko/|ModernGekko-${SHORT}/|" \
      -czf "$DEST/ModernGekko-${SHORT}.tar.gz" -T -
ls -lh "$DEST/ModernGekko-${SHORT}.tar.gz" | awk '{print "    "$5"  "$9}'
archive_tree ModernGekko-dolphin ModernGekko/vendor/dolphin
archive_tree DolRecomp DolRecomp

# Build/packaging scripts that are not in the three upstream trees.
git -C "$REPO" archive --format=tar.gz --prefix="RingOut-arm64-${SHORT}/" HEAD \
  arm64 patches dist/RingOut-1.0-dist dist/shared dist/RingOut-1.0-deck \
  README.md LICENSE VERSION .gitignore docs \
  > "$DEST/RingOut-arm64-${SHORT}-extras.tar.gz"
ls -lh "$DEST/RingOut-arm64-${SHORT}-extras.tar.gz" | awk '{print "    extras "$5}'

# Same guards that are already committed; kept as a standalone patch so a
# clean upstream checkout can be reproduced without the extras tarball.
if [ -f "$REPO/patches/0001-linux-aarch64-port-guards.patch" ]; then
  cp -a "$REPO/patches/0001-linux-aarch64-port-guards.patch" "$DEST/"
fi

cat > "$DEST/README.txt" <<EOF
Ring Out $SHORT — Linux aarch64 corresponding source
====================================================

These archives are the source used to build the aarch64 runtime and
recompiler in this zip (commit $FULL).

  ModernGekko-${SHORT}.tar.gz          chassis / runtime
  ModernGekko-dolphin-${SHORT}.tar.gz  vendored Dolphin
  DolRecomp-${SHORT}.tar.gz            static recompiler
  RingOut-arm64-${SHORT}-extras.tar.gz packaging, arm64 scripts, dist scaffolding
  0001-linux-aarch64-port-guards.patch x86-only fmod/--deck/PGO/glibc guards

The three trees are vendored in one git repository, so every archive is
pinned to the same parent commit rather than three nested HEAD shas.

Public copy: https://github.com/NagaseKouichi/RingOut-arm64
EOF

{
  echo "commit $FULL"
  echo
  ls -1 "$DEST"
} > "$DEST/MANIFEST.txt"

echo
echo "==> privacy check"
"$REPO/.github/scripts/privacy-scan.sh" "$DEST"

echo
echo "==> $DEST"
du -sh "$DEST" "$DEST"/*
