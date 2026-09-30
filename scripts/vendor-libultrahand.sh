#!/usr/bin/env bash
#
# Vendors libultrahand as a build-time dependency.
#
# libultrahand is neither committed to the repo nor used as a git submodule:
# this script grabs it at build time, straight into its destination. Nothing is
# copied around, and re-running it when the ref has not moved does nothing.
#
# Place it at <repo>/scripts/vendor-libultrahand.sh. The repo root is worked out
# from this file's own location, so it runs correctly from any directory.
#
# The ref can be a branch, a tag, or a commit SHA, via LIBULTRAHAND_REF
# (default: v2.4.3). 'master' and 'main' both mean "the default branch", so
# either spelling works regardless of which branch libultrahand actually uses.
#
# Usage: scripts/vendor-libultrahand.sh [dest-dir]
set -euo pipefail

REF="${LIBULTRAHAND_REF:-v2.4.3}"

# Repo root = the parent of the directory holding this script, so the default
# destination does not depend on the caller's working directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# An explicit destination (argument or env var) is taken as given, relative to
# where you ran the script. The default is relative to the repo root.
DEST="${1:-${LIBULTRAHAND_DEST:-$ROOT/overlay/lib/libultrahand}}"

REPO="https://github.com/ppkantorski/libultrahand.git"

# --- what commit does REF mean? ----------------------------------------------
# Resolved first, so an existing checkout can be compared against it. Anything
# left over from a different ref - a stale submodule, an older pin - is replaced
# rather than silently trusted.
if [ "$REF" = master ] || [ "$REF" = main ]; then
    WANT="$(git ls-remote "$REPO" HEAD | cut -f1)"
else
    WANT="$(git ls-remote --tags "$REPO" "refs/tags/${REF}^{}" | cut -f1)"          # annotated tag
    [ -n "$WANT" ] || WANT="$(git ls-remote --tags "$REPO" "refs/tags/${REF}" | cut -f1)"   # lightweight tag
    [ -n "$WANT" ] || WANT="$(git ls-remote "$REPO" "refs/heads/${REF}" | cut -f1)"         # branch
    if [ -z "$WANT" ] && printf '%s' "$REF" | grep -Eq '^[0-9a-f]{7,40}$'; then
        WANT="$REF"                                                                # bare commit SHA
    fi
fi

if [ -z "$WANT" ]; then
    echo "error: LIBULTRAHAND_REF='$REF' does not resolve in $REPO" >&2
    exit 1
fi

HAVE="$(git -C "$DEST" rev-parse HEAD 2>/dev/null || true)"
case "$HAVE" in
    "$WANT"*)
        echo "libultrahand@$REF already vendored at $DEST (${HAVE:0:12})."
        exit 0
        ;;
esac

echo "Vendoring libultrahand@$REF into $DEST ..."
rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"

# Fast path: a branch or tag tip can be shallow cloned.
clone_args=(--depth 1)
case "$REF" in
    master|main) ;;                       # no --branch: take the default branch
    *) clone_args+=(--branch "$REF") ;;
esac

if git clone --quiet "${clone_args[@]}" "$REPO" "$DEST" 2>/dev/null; then
    echo "Cloned libultrahand@$REF (${WANT:0:12})."
    exit 0
fi

# Fallback: a commit SHA, or any ref --branch cannot take, needs a full clone.
echo "'$REF' is not a branch or tag tip - doing a full clone and checkout ..."
rm -rf "$DEST"
git clone "$REPO" "$DEST"
git -C "$DEST" checkout --quiet "$REF"
echo "Checked out libultrahand@$REF (${WANT:0:12})."
