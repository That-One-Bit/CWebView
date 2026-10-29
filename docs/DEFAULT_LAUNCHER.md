# Optional platform-aware launcher

`test_pages/default_launcher.html` is an optional single-file launcher/backup
page. It uses `navigator.platform`, supplied by CWebView's limited navigator
object, to choose a platform-specific controls summary. It is not forced as the
app's home page: copy it into the app's `web/` folder and select
`default_launcher.html` as the package's `webpage`/launch page only when you
want this backup launcher.

This is intentionally a small platform-identification surface, not full browser
compatibility. `navigator.platform` and `navigator.userAgent` identify CWebView's
native platform; other navigator APIs (language, hardware concurrency, online
state, etc.) are not implied to exist.

## Replacing the malformed pixel fixture

The previous `webapp/nested/pixel.png` failed the CWI packer's PNG scanline
validation. A clean 8x8 RGBA replacement is included at
`test_pages/replacement-pixel.png`. Copy it over your local
`webapp/nested/pixel.png` if you want to retain the same fixture path. The
build archive intentionally does not include the user's `webapp/` tree, so the
replacement is also supplied as a standalone PNG.
