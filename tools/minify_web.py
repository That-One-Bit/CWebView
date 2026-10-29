#!/usr/bin/env python3
"""Build-time webapp minifier for Vita WebView.

Usage:  python3 minify_web.py <source_dir> <output_dir>

The readable webapp sources stay the single source of truth; this tool
copies them to the build staging area and applies a CONSERVATIVE minify:

  * .html  - HTML comments removed; per-line leading/trailing whitespace
             trimmed; runs of blank lines collapsed.  <pre>, <textarea>
             and <script> blocks are stashed verbatim so their exact
             whitespace is preserved (template literals in JS must not
             change).  <style> blocks get the CSS minifier.
  * .css   - comments removed, whitespace collapsed, spaces around
             {}:;,>+~ dropped, trailing ';' before '}' dropped.
  * .js    - copied UNTOUCHED (minifying JS safely needs a real parser;
             the runtime savings are not worth the risk).

Safety rules:
  * The output directory must NOT be the source directory or inside it,
    and the source must not be inside the output.  The tool refuses
    instead of copying/deleting in place.
  * Files that are not valid UTF-8 are copied BYTE-FOR-BYTE with a
    warning instead of crashing the whole build (a stray cp1252 byte
    in an HTML file must not fail packaging).
  * A failed file names the exact path and reason.

Files with unknown extensions are copied byte-for-byte.  A minified file
that somehow parses differently is a bug - report the input to fix it.
"""
import re
import shutil
import sys
from pathlib import Path

VERBATIM = re.compile(r"(<(pre|textarea|script)\b[^>]*>).*?(</\2\s*>)",
                      re.I | re.S)
STYLE = re.compile(r"(<style\b[^>]*>)(.*?)(</style\s*>)", re.I | re.S)
COMMENT = re.compile(r"<!--.*?-->", re.S)

FAILURES = []          # (path, reason) pairs reported at the end


def minify_css(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"\s+", " ", text)
    text = re.sub(r"\s*([{}:;,>+~])\s*", r"\1", text)
    text = text.replace(";}", "}")
    return text.strip()


def minify_html(text: str) -> str:
    stash = []

    def keep(m):
        stash.append(m.group(0))
        return "\x00%d\x00" % (len(stash) - 1)

    text = VERBATIM.sub(keep, text)          # protect verbatim regions
    text = STYLE.sub(lambda m: m.group(1) + minify_css(m.group(2)) + m.group(3),
                     text)                  # inline CSS gets minified
    text = COMMENT.sub("", text)            # HTML comments
    text = "\n".join(line.strip() for line in text.splitlines())
    text = re.sub(r"\n{2,}", "\n", text)    # blank-line runs
    for i, block in enumerate(stash):
        text = text.replace("\x00%d\x00" % i, block)
    return text


def minify_file(src: Path, dst: Path) -> None:
    """Copy src -> dst, minifying HTML/CSS.  Never raises: problems are
    recorded in FAILURES and the file is copied verbatim instead."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    data = src.read_bytes()
    kind = src.suffix.lower()
    if kind not in (".html", ".css"):
        dst.write_bytes(data)               # .js + everything else: verbatim
        return
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        # Not valid UTF-8 (a cp1252/latin-1 byte is common in Windows-
        # authored pages).  Minifying would corrupt it; copy untouched.
        dst.write_bytes(data)
        FAILURES.append((src, "not valid UTF-8, copied without minifying"))
        return
    try:
        out = minify_html(text) if kind == ".html" else minify_css(text)
    except Exception as e:                  # regex surprise: copy verbatim
        dst.write_bytes(data)
        FAILURES.append((src, "minifier error (%s), copied verbatim" % e))
        return
    dst.write_bytes(out.encode("utf-8"))


def fail(msg: str) -> int:
    print("minify_web: %s" % msg, file=sys.stderr)
    return 1


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src_dir = Path(sys.argv[1]).resolve()
    out_dir = Path(sys.argv[2]).resolve()

    if not src_dir.is_dir():
        return fail("source directory does not exist: %s" % src_dir)
    if src_dir == out_dir:
        return fail("output directory must differ from the source directory "
                     "(refusing to modify files in place): %s" % src_dir)
    if out_dir in src_dir.parents:
        return fail("output directory is the source's parent (%s) - deleting "
                    "it would delete the source; refusing" % out_dir)
    if src_dir in out_dir.parents:
        return fail("output directory is inside the source directory (%s); "
                    "refusing" % src_dir)

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    count = 0
    for src in sorted(src_dir.rglob("*")):
        if src.is_dir() or src.name.startswith("."):
            continue
        try:
            minify_file(src, out_dir / src.relative_to(src_dir))
            count += 1
        except OSError as e:
            FAILURES.append((src, str(e)))
    print("minified %d files: %s -> %s" % (count, src_dir, out_dir))

    # Warnings never stop the build (the files were still copied), but they
    # are loud enough to notice in the build log.
    for path, reason in FAILURES:
        print("minify_web WARNING: %s: %s" % (path, reason), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
