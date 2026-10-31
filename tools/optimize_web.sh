#!/usr/bin/env bash
# optimize_web.sh - ONE web-content optimization stage for every port
# (user direction, 2026-10-07 (83): "tighten the bond between
# compression, minification and optimization").
#
#   optimize_web <src-dir> <dest-dir> [label]
#
# Stages src -> dest through the full compacted pipeline, in order:
#   1. copy/minify  - tools/minify_web.py (HTML comments + CSS
#      whitespace removed; JS and binaries copied untouched); a
#      minification failure falls back to a verbatim copy, loudly, and
#      NEVER aborts the build
#   2. CWI packing  - tools/cwi_pack_stage.sh converts staged PNG/BMPs
#      in place (growth-guarded, idempotent, degrades gracefully)
#   3. a one-line savings report tying both steps together
#
# The readable webapp/ sources stay the single source of truth; only the
# STAGED copy is compacted.  Ports that previously staged verbatim
# (Wii U, Windows) now get the same minified + packed content as the
# console builds - identical bytes, same rules, everywhere.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/tools/cwv_term.sh"

_tree_bytes() { find "$1" -type f -printf '%s\n' 2>/dev/null | awk '{s+=$1} END{print s+0}'; }

optimize_web() {
  local src="${1:-}" dest="${2:-}" label="${3:-opt}" before after pct
  [[ -d "$src" ]] || { cwv_err "${label}: no web content to stage (${src:-empty})"; return 1; }
  rm -rf "$dest"
  mkdir -p "$(dirname "$dest")"
  before=$(_tree_bytes "$src")
  # 1. minification (optimization, never correctness - degrade, not die)
  if ! python3 "$ROOT/tools/minify_web.py" "$src" "$dest" >/dev/null 2>&1; then
    if command -v python3 >/dev/null 2>&1; then
      cwv_warn "${label}: *** WEBAPP MINIFICATION FAILED (tools/minify_web.py) - shipping the UNMINIFIED webapp instead ***"
    else
      cwv_warn "${label}: python3 not found - staging the webapp verbatim (loads fine, just larger)"
    fi
    rm -rf "$dest"
    mkdir -p "$dest"
    cp -r "$src"/. "$dest"/
  fi
  # 2. CWI packing over the staged copy
  # shellcheck disable=SC1091
  source "$ROOT/tools/cwi_pack_stage.sh"
  cwi_pack_stage "$dest" "$label"
  # 3. combined savings line
  after=$(_tree_bytes "$dest")
  if [[ "$before" -gt 0 ]]; then
    pct=$(awk "BEGIN{printf \"%.1f\", ($after-$before)*100/$before}")
    if [[ "$after" -lt "$before" ]]; then
      cwv_ok "[opt] web content compacted: $((before/1024)) KB -> $((after/1024)) KB ($pct%)"
    else
      cwv_ok "[opt] web content staged verbatim: $((before/1024)) KB (minified/CWI gains offset by packed fast-load payload)"
    fi
  fi
  return 0
}

# direct execution: optimize_web <src> <dest> [label]
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  optimize_web "${1:-}" "${2:-}" "${3:-opt}" || exit 1
fi
