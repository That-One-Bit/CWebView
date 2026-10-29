# Minimal Web Test Page

The repository's default `webapp/index.html` is intentionally tiny: one heading, one paragraph, and three CSS-colored blocks.

It contains no JavaScript, external resources, fonts, images, or network dependencies. This makes it the first rendering target for the Vita WebKit backend.

Expected screen:

- `CWebView` heading
- explanatory paragraph
- red, green, and blue boxes

If the native wrapper builds and launches but this page does not appear, the problem is below the webpage layer (WebKit initialization, view creation, rendering, or the native/WebKit bridge).

## Nested-renderer regression test

The current renderer should also be tested with genuinely nested markup, not only flat elements:

```html
<header style="background:#1e293b; color:#ffffff; padding:12px;">
  <h1>CWebView</h1>
  <p>Header paragraph</p>
</header>
<main>
  <div style="background:#334155; color:#ffffff; padding:16px;">
    <h2>Renderer milestone</h2>
    <p>Nested content should be normal text, not literal HTML tags.</p>
  </div>
</main>
```

Expected behavior:

- No `<header>`, `<h1>`, `<div>`, or `</...>` strings appear on screen.
- `h1` and `h2` are visibly larger than paragraph text.
- The header and inner div backgrounds are visible.
- Text inside the containers retains the requested color.
- Nested content is laid out vertically rather than disappearing.


### Native focus navigation

The native renderer currently cycles focus through links and buttons in document order. `tabindex` is intentionally not implemented yet.

- D-pad Down: Tab / next focusable element
- D-pad Up: Shift+Tab / previous focusable element
- Focused controls receive a yellow outline.
- Focus navigation automatically scrolls the focused control into view.

- `textfield.html` — CWV `<textfield>` + PSVita IME/keyboard test.

## `cwv-button-wait`

CWebView page input remains edge-triggered by default: a held Vita button does not generate another page press until it is released and pressed again. A page can explicitly choose a hold/repeat policy on its `<html>` element without using a `data-` prefix:

```html
<html cwv-button-wait="release">
```

Supported values include:

- `release` — default when explicitly supplied; wait for release before another press.
- `none` — repeat immediately while the physical button remains held.
- `1s` — wait 1 second after the initial press, then repeat every 1 second.
- `1.5s 0.5s` — wait 1.5 seconds after the initial press, then repeat every 0.5 seconds.

The policy applies to Vita page controls/keyboard input, not the native CWebView menus. Native menus always use release-to-repeat behavior so a held control cannot accidentally skip several menu items.

## Modern JavaScript regression pages

- `test_pages/es6_selftest.html`: existing ES2015 feature coverage.
- `test_pages/es7_selftest.html`: spread, destructuring and for-of cases.
- `test_pages/es_class_selftest.html`: class constructors/methods, accessors,
  inheritance, field initialization order, private-name execution and comments.

The mirrored pages in `desktop_test/testdata/` are exercised through the actual
load, transform, compile, execute and rendered-result path by
`bash desktop_test/build_es2015_test.sh`. Failed assertions must fail the gate,
not merely print a diagnostic. The class page is execution coverage, not a
claim of full language conformance or enforced private-member privacy.

Use the same class page on the console for runtime validation. Desktop
results cannot validate the console SDK, module loader or final package.
