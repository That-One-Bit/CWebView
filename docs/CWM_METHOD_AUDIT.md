# CWMenu method and exposure audit

Audit basis: the current `cwmenu_api.h`, `cwmenu_api.c`, `cwv_menu_ui.h`, `cwv_menu_ui.c`, and `docs/CWM_SCRIPT.md`. This is a source-tree audit, not a claim that every method has been exercised on physical consoles.

## Public C APIs and their current role

| API / capability | Current exposure | Finding |
|---|---|---|
| `cwmenu_script_load`, `cwmenu_script_link` | Public C API; host calls them | Implemented. Linking merges project defaults/variables and replaces same-named menu blocks. |
| `cwm_project_set/get/is_on/clear` | Public C API; host injects feature flags | Implemented. Project defaults fill only unset names. |
| `cwm_theme_get_rgb`, `cwm_theme_get_label`, `cwm_theme_count` | Public C API; renderer consumes theme slots | Implemented for named stored slots and the documented accent labels. |
| `cwm_dynamic_menu`, `cwm_dynamic_menu_total/name`, `cwmenu_script_menu_count` | Public C API; used for custom menu lookup and validation | Implemented, bounded by the fixed dynamic registry capacity. The language extension warns about unresolved local pair targets but explains linked scripts may define them. |
| `cwm_var_set_number`, `cwm_var_get_number` | Public C API; renderer uses variables for slider stops, brightness, and tint | Partially exposed at the language level. `var` declares numeric state, but `value` and `progress` elements do not yet bind their displayed output to `$var`; there is no documented string/boolean variable type or general interpolation. |
| `cwm_scrollbar_*` | Public C API; shared UI scrollbar math | Implemented as a geometry primitive. The `.cwm` scrollbar group syntax is parsed; the active viewport and content list still depend on host integration. |
| `pair <menu>` | Script key, not a C method | Implemented as passive partner rendering, not an independent interactive window. This is documented behavior, not a hidden API. |
| `open_script:<file>` | Host action convention | Implemented only on ports whose host supplies `load_script`; other ports can leave it inert. |

## Partially created / not fully exposed capabilities

1. **Reactive numeric store** — the C API and `brightness $var`, `tint $var`, and `stops $var` already exist. General live `value`/`progress` display bindings are the most obvious missing language-level exposure.
2. **Multi-window pairing** — a partner template is rendered and supports most static element types, but input/focus/lifecycle are intentionally absent for the partner. True independent windows require a new runtime model, not just a missing key.
3. **Action dispatch** — `action` is stored as a single string and dispatched by the host. No typed/named argument list is parsed or delivered to action handlers. `open_script:<file>` is a special convention rather than a general parameterized action mechanism.
4. **Script links** — link/merge behavior is implemented, but path resolution and whether links work are host/port responsibilities. Diagnostics cannot reliably resolve targets without the host's runtime search paths.
5. **`scale` key** — the parser accepts and stores it for a window, clamping the value, but rendering behavior must be verified per port; this key is distinct from `size` (chrome scale) in the public struct and docs are not clear about its independent visual meaning.

## Proposed versioned language additions

- `bind value $fps format "%.1f FPS"` or a similarly explicit binding for `value` and `progress`, with numeric value/min/max and a redraw-on-change contract.
- Parameterized actions, e.g. `action open_url url "$target"`, using quoted argument values and a host callback that receives a stable key/value array. Existing single-token `action name` remains valid.
- An opt-in interactive window declaration with per-window focus/input routing. Preserve `pair` as passive display-only mode so existing scripts do not change behavior.

These are candidates, not implemented language syntax. The exact grammar should be agreed before it becomes part of the versioned CWM contract.
