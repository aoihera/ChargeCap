#!/usr/bin/env bash
#
# Vendors libultrahand into overlay/lib/libultrahand.
#
# We deliberately do not use git submodules. Run this once, commit the result,
# and the repo is self-contained from then on. CI runs it too, so a fresh clone
# builds without any extra steps.
#
# Override the pinned revision with LIBULTRAHAND_REF (tag, branch or commit).
set -euo pipefail

REF="${LIBULTRAHAND_REF:-v2.4.3}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/overlay/lib/libultrahand"

if [ -f "$DEST/ultrahand.mk" ]; then
    echo "libultrahand already vendored ($(cat "$DEST/.vendored-ref" 2>/dev/null || echo unknown)). Delete $DEST to re-vendor."
    exit 0
fi

echo "Vendoring libultrahand @ $REF ..."

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

curl -fsSL "https://codeload.github.com/ppkantorski/libultrahand/tar.gz/$REF" -o "$tmp/libultrahand.tar.gz"
tar -xzf "$tmp/libultrahand.tar.gz" -C "$tmp"

src="$(find "$tmp" -mindepth 1 -maxdepth 1 -type d -name 'libultrahand-*' | head -n1)"
if [ -z "$src" ]; then
    echo "error: unexpected archive layout" >&2
    exit 1
fi

mkdir -p "$DEST"
for item in common libultra libtesla ultrahand.mk LICENSE SUB_LICENSE; do
    if [ -e "$src/$item" ]; then
        cp -R "$src/$item" "$DEST/"
    else
        echo "warning: $item not present in libultrahand@$REF" >&2
    fi
done

printf '%s\n' "$REF" > "$DEST/.vendored-ref"
echo "Vendored into overlay/lib/libultrahand (ref: $REF)"
