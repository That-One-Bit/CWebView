#!/usr/bin/env bash
# clean.sh - remove ALL build outputs from the tree, every platform at once.
#
# What gets removed:
#   build/                    Vita CMake tree, Wii U ELF/RPX/WUHB + SD staging,
#                             ESM extension builds (everything the build
#                             scripts and the Makefile generate under build/)
#   <root>/<TARGET>.vpk       VPK that the Makefile's `vpk` target copies next
#                             to the repo (e.g. CWebView.vpk / VitaWebview.vpk)
#   /tmp/cwv_*_wii*.o         scratch objects from the wii/wiiu compile checks
#
# What is NEVER touched: sources/, webapp/, assets/, docs/, the git repo,
# and anything not produced by a build.
#
# Usage:
#   ./clean.sh            interactive (asks once, shows what will go)
#   ./clean.sh -y         no confirmation
#   ./clean.sh -n         dry run: show what would be removed, change nothing
#   ./clean.sh -h         this help
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASSUME_YES=0
DRY_RUN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes) ASSUME_YES=1 ;;
    -n|--dry-run) DRY_RUN=1 ;;
    -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "clean.sh: unknown option '$1' (try -h)" >&2; exit 2 ;;
  esac
  shift
done

# ---- collect the removal set -----------------------------------------------
TARGETS=()
[[ -d "$ROOT/build" ]] && TARGETS+=("$ROOT/build")
for vpk in "$ROOT"/*.vpk; do
  [[ -f "$vpk" ]] && TARGETS+=("$vpk")
done
for o in /tmp/cwv_main_wii.o /tmp/cwv_main_wiiu.o /tmp/cwv_platform_wii.o /tmp/cwv_platform_wiiu.o; do
  [[ -f "$o" ]] && TARGETS+=("$o")
done

if [[ ${#TARGETS[@]} -eq 0 ]]; then
  echo "clean: nothing to remove - the tree is already clean."
  exit 0
fi

# ---- show what will go -------------------------------------------------------
echo "clean: removing build outputs:"
total=0
for t in "${TARGETS[@]}"; do
  if [[ -d "$t" ]]; then
    sz="$(du -sh "$t" 2>/dev/null | cut -f1)"
  else
    sz="$(du -h "$t" 2>/dev/null | cut -f1)"
  fi
  total=$((total + 1))
  echo "  [$sz]  $t"
done
echo "  (${total} item(s))"

if [[ $DRY_RUN -eq 1 ]]; then
  echo "dry run: nothing removed."
  exit 0
fi

if [[ $ASSUME_YES -eq 0 ]]; then
  read -r -p "Proceed? [y/N] " answer || true
  case "${answer:-n}" in
    y|Y|yes|YES) ;;
    *) echo "aborted."; exit 1 ;;
  esac
fi

# ---- remove ------------------------------------------------------------------
failed=0
for t in "${TARGETS[@]}"; do
  rm -rf -- "$t" || { echo "clean: could not remove $t" >&2; failed=1; }
done
((failed)) && { echo "clean: finished with errors."; exit 1; }
echo "clean: done. Rebuild with ./build-vita.sh / ./build-wiiu.sh / ./build-wii.sh"
