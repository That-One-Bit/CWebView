#!/usr/bin/env bash
# Runs every desktop build gate (2026-10-01). This is the pre-ship
# checklist: every gate must report PASS (SKIP only where a toolchain is
# genuinely absent, e.g. the Windows cross-compilers on a bare console
# build machine). Run from anywhere.
set -u
cd "$(dirname "$0")"
logs="/tmp/cwv-gate-logs"
mkdir -p "$logs"
failed=0
for gate in check_reference.sh build_position_edges_test.sh build_main_gate.sh build_js_test.sh build_canvas2d_test.sh build_es2015_test.sh build_cwmenu_unit.sh build_loghook_test.sh build_wiiu_gate.sh build_wii_gate.sh build_vita_kbm_gate.sh es2015_unit/build_unit.sh build_paths_check.sh build_shared_gate.sh build_approot_test.sh build_cursor_test.sh build_img_cache_test.sh build_menu_hotkey_test.sh build_menu_ui_test.sh build_theme_test.sh build_es2015_portability_gate.sh build_bmp_unit.sh build_cwi_unit.sh build_term_gate.sh build_win_gate.sh build_ts_unit.sh build_error_audit.sh build_es_editions_unit.sh; do
    name="${gate//\//_}"
    echo "RUN $gate"
    if bash "$gate" > "$logs/$name.log" 2>&1; then
        echo "PASS $gate"
    else
        echo "FAIL $gate"
        tail -25 "$logs/$name.log"
        failed=$((failed+1))
    fi
done
echo "FAILURES=$failed"
exit "$failed"
