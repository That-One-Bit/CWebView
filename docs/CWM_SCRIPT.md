# The CWMenu Script (Vita Webview Menu DSL)

`cwmenu/default.cwm` defines the native menu system - windows, headers, tab rows,
buttons, sliders, badges, scrollbars, colors - in a plain-text DSL. An
application built with Vita WebView ships its own `.cwm` file and gets its own
settings, DevTools, and app-specific menus without any C changes.

This document is the full reference for **CWMenu 4**, including the compatible CWMenu 2 window/theme/element syntax, CWMenu 3 project conditionals, and CWMenu 4 script linking.

**CWMenu feature index** (every capability, one line each):

- **Window keys** on the `window` element: `anchor x y`, `size s`,
  `corner sharp|rounded`, `crop l t r b`, `pair <menu>` (dual-window
  partner drawn above the active menu, display-only), `scale`.
- **Theme block**: the script owns every menu color slot (`overlay`,
  accent, text colors...) - re-skin the whole native shell without C.
- **Element kinds**: window, panel, header, button, text, underline,
  slider, checkbox, segmented, badge, footer, decoration, icon, progress,
  value, list, shape_rectangle, shape_rounded_rectangle, shape_circle,
  line, scrollbar.
- **Common element keys**: `action`, `stops N`, `stops $var`,
  `brightness $var`, `tint <0..1>`, `tint $var`, `pair`, and
  `border <sides>` (text is PLAIN by default; opt into chrome).
- **Script variables**: `var name value` lines; elements read them with
  `$name` (sliders can drive them back).
- **Dynamic app menus**: `menu <name>` blocks that no C target claims are
  registered automatically - an app can add its own menus with no C changes.
- **Script linking (CWMenu 4)**: `open_script:<file>` lets a host merge a second script; linked menu blocks replace the named menu, project defaults merge without overriding existing values, and theme slots use last-write-wins.
- **Project conditionals (CWMenu 3)**: `project` defaults and `when <feature>` gates let app builds hide unavailable rows without editing the shared script.
- **Text wrapping**: every text element wraps its label inside the
  element's own width (word boundaries, hard break for oversized words);
  when wrapped lines would exceed the element height, the last visible
  line ends with `...` - text can never run past its window.
- **Actions**: the `action` string is the host dispatch key (elements can
  be renamed freely); the host action list is owned by main.c. The controller (the
`cwmenu_api.c` parser plus the menu renderer in `main.c`) parses **any**
version header and ignores constructs newer than itself, so a CWMenu 2 script
still loads in an older host with the unknown lines skipped.

## File layout

```
CWMenu 2                      <- version header (parsed, any number accepted)

theme                      <- optional color block (CWMenu 2)
    accent0 #466996
    label0 "Blue"
    ...
end

var pitch_stops 6          <- script variables ($name in element lines)

menu main                  <- a named menu block
element window window 0 0 740 436 normal ""
element back button 20 376 150 38 normal "Back" action back
end
```

Blank lines and `#` comment lines are ignored everywhere.

## Menus and elements

```
menu <name>
element <element-name> <kind> <x> <y> <w> <h> <style> "<label>" [keys...]
end
```

- `x y w h` are in window-local pixels (the window itself is `0 0 740 436`).
- `style` is `normal`, `light`, or `dim` (color tinting, see theme below).
- The label is a quoted string; everything after the closing quote is parsed
  as `key value` pairs (keys are matched by prefix, order does not matter).

### Element kinds

| Kind | Use |
|---|---|
| `window` | The menu's window. One per menu; owns the geometry keys below. |
| `header` / `footer` | Chrome bars; the label is drawn in them. |
| `panel` | Background panel (inset content area). |
| `button` | Tappable row/button. |
| `text` | Static text label. |
| `slider` | Slider; needs `stops N` (2..6). |
| `checkbox` / `segmented` / `list` | Choice controls. |
| `badge` | Small status pill (label is the badge text). |
| `progress` / `value` | Progress bar / value readout. |
| `underline` | Thin separator line. |
| `decoration` | Decorative accent block. |
| `icon` | Icon slot. |
| `shape_rectangle` / `shape_rounded_rectangle` / `shape_circle` / `line` | Shapes. |
| `scrollbar` | See "Scrollbar groups" below. |

### Common element keys

| Key | Meaning |
|---|---|
| `action <name>` | String the host dispatches on activation (e.g. `settings`, `back`, `menu-tests`). |
| `stops N` | Slider stop count. `stops $var` reads the count from a `var`. |
| `brightness $var` | Slider brightness driven by a variable. |
| `tint <0..1>` or `tint $var` | Accent tint mixing. |
| `border <sides>` | Optional border chrome, comma/space separated: `top right bottom left all fill none`. Text elements are PLAIN by default (no panel, no edges); `fill` adds the soft panel background. Example: `element note text 0 0 200 24 normal "Hi" border bottom,left` |

**Text wrapping (CWMenu 2):** every text element wraps its label to the
element's own width - long labels wrap at word boundaries instead of
running over the window edge.  When the wrapped lines would be taller than
the element, the last visible line ends with `...`.  Give the element more
height in the `.cwm` file to show more wrapped lines; the width that
matters is always the element's own `w`.

## Theme block (CWMenu 2)

The script owns every menu color slot. This is how an app re-skins the whole
native shell - accent colors, text colors, chrome base, overlay - without
touching C. Colors are `#RRGGBB` (or `R G B` decimals):

```
theme
    accent0 #466996        <- the Settings "Accent color" row cycles accent0..5
    accent1 #B43737
    accent2 #CD7030
    accent3 #379155
    accent4 #2D9BAF
    accent5 #5C3787
    label0 "Blue"          <- display names for the same accents (labelN pairs
    label1 "Red"              with accentN; quoted string required)
    text-normal #EBF0FA    <- text color of `normal`-style elements
    text-light #FFF8EE     <- `light`
    text-dim #9BA5B4       <- `dim`
    neutral #96A0AF        <- un-tinted chrome base
    overlay #000000        <- full-screen backdrop rgb (alpha stays fixed)
end
```

Omitted slots keep the built-in C fallbacks, so a theme block can override a
single color (just `overlay`) or the entire palette. An unknown slot name is
a parse error and the whole load reports the error count.

## Window keys (CWMenu 2)

The `window` element accepts optional geometry keys:

```
element window window 0 0 740 436 normal "" pair tests_windows_pair
element window window 0 0 300 200 normal "" anchor 560 120 size 1.0 corner rounded
```

| Key | Meaning |
|---|---|
| `anchor X Y` | Absolute on-screen position of the window (the default is 110,54). Element `x y` still apply as offsets from the anchor. |
| `size S` | Chrome scale, clamped to `0.75 .. 1.25` (1.0 = default). |
| `corner rounded` or `corner sharp` | Rounded (12px) or square window corners. |
| `crop L T R B` | Chrome insets in pixels (trim the window edges). |
| `pair <menu>` | Render `<menu>` as a live second window beside this one (dual-window mode). |

### Dual windows (`pair`)

A menu whose window carries `pair <menu>` renders that partner menu as a
second window - with its own anchor / size / corner / crop, its own header,
and a static pass of its elements (buttons, text, badges, sliders, shapes).
Rules, by design:

- The **active** menu keeps all input; the partner is display-only. This is
  paired rendering, not two independent interactive/native OS windows.
- One level deep: a partner's own `pair` key is ignored.
- The partner renders *after* the primary content, so it paints on top
  wherever the two windows overlap (dual windows sit beside or over the
  active menu, never behind it).
- The partner paints its supported static elements (panels, buttons, text,
  footers, separators, sliders, checkboxes, segmented/list controls, badges,
  icons, progress/value displays, decorations, and shapes). Scrollbars are
  omitted because they require an active viewport/scroll state.
- Text boxes wrap at word boundaries, hard-break oversized words, and
  ellipsize the last visible line when their height is insufficient on both
  the Vita-native renderer and the shared Windows/Wii/Wii U renderer.
- The partner is looked up by name through the same registry the host uses
  for compiled-in menus (`vita_native_menu_find`), so it can be a target
  menu OR a dynamic app menu.

Menu Tests -> advanced page -> the **Windows** tab is a live demo: its five
toggles edit the partner window template at runtime (size / corner / crop /
pair on-off / anchor offset). The **first page** is the tour of element
kinds, now spread over 8 tabs (Basics / Text / Controls / Data / Shapes /
Lines / Styles / More) with taller rows, so each demo is a bigger touch
target; DevTools > Platform shows which menu script loaded and from where
(packaged default vs. a per-app data-dir override), and Settings >
Appearance draws the zoom control as a real 6-stop slider (0.75x..2.0x).

## Dynamic app menus

Any `menu <name>` block whose name is not one of the host's compiled-in
targets is still parsed and stored in a dynamic registry (8 menus, 32
elements each). Applications use this to define their own settings pages,
devtools, or custom menus and then reference them - including as a `pair`
partner. Nothing in C has to know the menu exists.

## Scrollbar groups

Scrollbars are declared CSS-style, attached to a menu:

```
content scrollbar {
    track 708 108 5 210 light
    items content-row-0|content-row-1|content-row-2
}
```

`item <name>` lines append one name; `items a|b|c` appends several. The
generated element is named `scrollbar:<owner>` with action `scrollbar`; the
host shows it automatically when the listed content exceeds the viewport.

## Versioning and compatibility

- `CWMenu <n>` header: the controller records it (`cwmenu_script_version`)
  and warns when the script is newer than itself, but always loads.
- Unknown keys inside an element are ignored (forward compatibility).
- Unknown constructs in a newer file are counted, not fatal.
- The desktop unit test `desktop_test/build_cwmenu_unit.sh` verifies the whole
  contract: version header, theme slots, window keys, pair lookup, zoom
  slider, per-menu completeness, dynamic registry, and a CWMenu 3 probe.
