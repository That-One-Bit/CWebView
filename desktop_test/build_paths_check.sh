#!/usr/bin/env bash
# Static build audit: verifies the VPK build inputs resolve BEFORE the
# user runs build-vita.sh on a real VitaSDK machine.  The desktop cannot
# run the Vita toolchain, so this gate pins everything that can break a
# cross build: script syntax, every CMake-referenced source file, and
# the module export tables against the module sources.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
ok()  { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fail=1; }

for sc in build.sh sources/build-vita.sh sources/build-wii.sh sources/build-wiiu.sh sources/build-windows.sh tools/cwv_term.sh; do
  bash -n "$sc" && ok "$sc syntax" || bad "$sc syntax"
done
# every build script must use the shared palette (80)
for sc in sources/build-vita.sh sources/build-wii.sh sources/build-wiiu.sh sources/build-windows.sh; do
  grep -q 'cwv_term.sh' "$sc" && ok "$sc uses tools/cwv_term.sh" || bad "$sc does not source tools/cwv_term.sh"
done

# every source file mentioned in the root CMakeLists must exist
while read -r f; do
  [[ -f "$f" ]] && ok "CMake source: $f" || bad "missing CMake source: $f"
done < <(grep -oE '(sources|tools)/[A-Za-z0-9_./-]+\.(c|h|in|cwm|xml|png|html)' CMakeLists.txt | sort -u)

# same for the module CMake files (paths are relative to the module dir)
for m in sources/vita/scroll_module; do
  while read -r f; do
    p="$m/$f"
    [[ -f "$p" ]] && ok "$p" || bad "missing module source: $p"
  done < <(grep -oE '(src|\.\./\.\./shared)/[A-Za-z0-9_./-]+\.[ch]' "$m/CMakeLists.txt" | sort -u)
done

# every exported NID in the remaining Vita module must name a function
# that exists in its compiled sources
for m in scroll_module; do
  d="sources/vita/$m"
  while read -r fn; do
    [[ -z "$fn" ]] && continue
    if grep -qE "\b$fn\b" "$d"/src/*.c "$d"/src/*.h 2>/dev/null; then
      ok "export $fn in $m"
    else
      bad "export $fn missing from $m sources"
    fi
  done < <(grep -oE '^[a-z_0-9]+:' "$d/exports.yml" | tr -d ':')
done

# the shared engine must not call a console SDK directly
leak=$(grep -rnE 'vita2d_|sceIo[A-Z]|sceIme[A-Z]|SceWChar16' \
        sources/shared/sources 2>/dev/null \
        | grep -v '^\S+: *\*' || true)
if [[ -z "$leak" ]]; then
  ok "shared engine free of console SDK calls"
else
  echo "$leak"; bad "console SDK call leaked into sources/shared"
fi

if [[ $fail -eq 0 ]]; then echo "PATHS CHECK PASSED"; else echo "PATHS CHECK FAILED"; exit 1; fi
