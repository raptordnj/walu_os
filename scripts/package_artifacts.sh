#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

DIST_DIR="$ROOT_DIR/dist"
PKG_DIR="$DIST_DIR/package"

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"/{kernel,userland,metadata}

if [[ -f build/waluos.iso ]]; then
  cp build/waluos.iso "$PKG_DIR/kernel/"
fi

if [[ -d userland/build/bin ]]; then
  cp userland/build/bin/* "$PKG_DIR/userland/" 2>/dev/null || true
fi

cat > "$PKG_DIR/metadata/manifest.txt" <<EOF_MANIFEST
build_date=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
kernel_iso=$( [[ -f "$PKG_DIR/kernel/waluos.iso" ]] && echo yes || echo no )
userland_bins=$(ls "$PKG_DIR/userland" 2>/dev/null | wc -l | tr -d ' ')
EOF_MANIFEST

TARBALL="$DIST_DIR/waluos-artifacts.tar.gz"
rm -f "$TARBALL" "$TARBALL.sha256"
tar -C "$PKG_DIR" -czf "$TARBALL" .
sha256sum "$TARBALL" > "$TARBALL.sha256"

echo "packaged artifacts:"
echo "  $TARBALL"
echo "  $TARBALL.sha256"
