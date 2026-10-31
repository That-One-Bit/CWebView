#!/usr/bin/env bash
# cwi_pack_stage.sh - CWI fast-load packing as a BUILD step (user
# direction, 2026-10-07 (82); tightened 2026-10-07 (83)).  Walks a STAGED
# web-content directory and converts every PNG/BMP in place into a CWI
# container (same filename, CWI magic bytes): the engine sniffs the magic
# at fetch time (cwv_backend.c cwv_cwi_is) and takes the pre-decoded
# fast path, so page URLs keep saying "foo.png" and need zero changes.
#
#   cwi_pack_stage <staged-dir> [label]
#
# Behavior:
#   - growth guard (83): CWI is a LOAD-SPEED format, not a size win -
#     it embeds the original file (lossless ORIG chunk) next to the
#     pre-decoded pixels, so it is ALWAYS >= the PNG.  Measured growth:
#     tiny art ~1.6x, flat UI panels ~2-3x, noisy photos 4x+.  The guard
#     ships the packed CWI only when it stays within CWI_MAX_GROWTH
#     times the original (default 1.5: near-incompressible pixel data
#     like noise textures and small flat art qualify, favicon-sized
#     files and deflate-friendly photos stay the smaller PNG, decoding
#     normally).  CWI_MAX_GROWTH=0 packs unconditionally (load-speed
#     priority over package size).
#   - stray-sibling cleanup (83): foo.cwi next to a packed foo.png is
#     unreachable by URL (pages request foo.png) and only bloats the
#     package, so staged copies are removed - the in-place packed PNG
#     carries the same payload.  Hand-referenced .cwi files with no
#     same-name PNG are kept.
#   - skips files that are already CWI (idempotent: re-running a build
#     over a previously packed staging dir converts nothing)
#   - skips the console packaging metadata folders (vita/ wii/ wiiu/)
#     - icon0.png/bg0.png/meta.xml art are consumed by the OS packers
#     (LiveArea, meta.xml, WUHB), which need REAL PNG bytes
#   - failure of python3 or of an individual image degrades gracefully:
#     the file ships as the original PNG (a slow but valid load) and a
#     warning is printed - matching the minify_web.py precedent
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/tools/cwv_term.sh"

cwi_pack_stage() {
  local dir="${1:-}" label="${2:-cwi}" n=0 skipped=0 failed=0 smaller=0 cleaned=0 f tmp osz nsz
  local growth="${CWI_MAX_GROWTH:-1.5}"
  if [[ -z "$dir" || ! -d "$dir" ]]; then
    cwv_warn "${label}: no staged content to pack (${dir:-empty path})"
    return 0
  fi
  cwv_step "[cwi] packing CWI fast-load images in ${dir#$ROOT/} (growth guard ${growth}x) ..."
  if ! command -v python3 >/dev/null 2>&1; then
    cwv_warn "${label}: python3 not found - shipping original PNGs (slow but valid loads)"
    return 0
  fi
  while IFS= read -r -d '' f; do
    # packaging metadata folders must stay real PNGs for the OS packers
    case "/$f" in
      */vita/*|*/wii/*|*/wiiu/*) skipped=$((skipped+1)); continue ;;
    esac
    # already packed (idempotent rebuild)
    if [[ "$(head -c4 "$f" 2>/dev/null)" == "CWIF" ]]; then
      continue
    fi
    tmp="${f}.cwipack$$"
    osz=$(stat -c%s "$f" 2>/dev/null || echo 0)
    if python3 "$ROOT/tools/cwi_pack.py" "$f" -o "$tmp" >/dev/null 2>&1; then
      nsz=$(stat -c%s "$tmp" 2>/dev/null || echo 0)
      if [[ "$growth" != "0" ]] && awk "BEGIN{exit !($nsz > $osz * $growth)}"; then
        # the fast path is not worth this much package bloat - keep the
        # smaller original (it decodes fine, just slower)
        rm -f "$tmp"
        smaller=$((smaller+1))
      else
        mv "$tmp" "$f"
        n=$((n+1))
      fi
    else
      rm -f "$tmp"
      failed=$((failed+1))
      cwv_warn "${label}: CWI pack failed for ${f#$dir/} - shipping the original image"
    fi
    # stray sibling: foo.cwi is unreachable next to foo.png (URLs ask for
    # the .png), so the staged copy is dead weight - drop it
    if [[ -f "${f%.*}.cwi" ]]; then
      rm -f "${f%.*}.cwi"
      cleaned=$((cleaned+1))
    fi
  done < <(find "$dir" \( -name '*.png' -o -name '*.bmp' \) -type f -print0 2>/dev/null)
  local msg="[cwi] packed ${n} image(s) for pre-decoded console loads"
  ((smaller)) && msg+=" (${smaller} kept smaller as PNG)"
  ((skipped)) && msg+=" (${skipped} packaging asset(s) kept as PNG)"
  ((cleaned)) && msg+=" (${cleaned} stray .cwi sibling(s) removed)"
  ((failed)) && msg+=" (${failed} failure(s))"
  cwv_ok "$msg"
  return 0
}

# direct execution: pack one directory
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  cwi_pack_stage "${1:-}" cwi
fi
