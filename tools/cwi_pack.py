#!/usr/bin/env python3
"""cwi_pack - convert PNG/BMP (or raw RGBA) into a lossless CWI container.

The output keeps BOTH the fast console payload (PXRL, RLE-coded exact
RGBA) and the ORIGINAL file bytes (ORIG chunk, deflate-compressed), so
tools/cwi_unpack.py can always restore the source file byte-for-byte.

Usage:  python3 cwi_pack.py <in.png|in.bmp|in.rgba> [-w W -h H] [-o out.cwi]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cwi_common import pack_cwi, read_png_rgba, read_bmp_rgba

def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__); return 2
    src = args[0]
    out = os.path.splitext(src)[0] + ".cwi"
    w = h = None
    i = 1
    while i < len(args):
        if args[i] == "-o" and i + 1 < len(args): out = args[i + 1]; i += 2
        elif args[i] == "-w" and i + 1 < len(args): w = int(args[i + 1]); i += 2
        elif args[i] == "-h" and i + 1 < len(args): h = int(args[i + 1]); i += 2
        else: print("bad arg:", args[i]); return 2
    with open(src, "rb") as f:
        orig = f.read()
    ext = os.path.splitext(src)[1].lower()
    if ext == ".png":
        px, pw, ph = read_png_rgba(src)
    elif ext == ".bmp":
        px, pw, ph = read_bmp_rgba(src)
    elif ext in (".rgba", ".bin", ".raw"):
        if not (w and h) or w * h * 4 != len(orig):
            print("raw input needs -w/-h matching the buffer"); return 2
        px, pw, ph = orig, w, h
    else:
        print("unsupported input:", ext, "(use .png, .bmp or .rgba)"); return 2
    cwi = pack_cwi(px, pw, ph, orig_bytes=orig, src_name=os.path.basename(src))
    with open(out, "wb") as f:
        f.write(cwi)
    print("%s: %dx%d  original %d bytes -> CWI %d bytes (PXRL %d%%, ORIG kept)" %
          (out, pw, ph, len(orig), len(cwi),
           round(100 * len(cwi) / max(1, len(orig)))))
    # SELF-CHECK: decode what we wrote and compare every byte
    from cwi_common import parse_cwi
    px2, w2, h2, orig2, _ = parse_cwi(cwi)
    assert (w2, h2) == (pw, ph) and px2 == px, "self-check: pixels differ!"
    assert orig2 == orig, "self-check: ORIG bytes differ!"
    print("self-check: pixel-exact + original preserved (zero loss)")
    if len(cwi) > len(orig):
        print("note: this image does not compress well as CWI (noisy/photographic);"
              " a plain PNG is smaller and loads fine - CWI shines on UI art"
              " with flat regions")
    return 0

if __name__ == "__main__":
    sys.exit(main())
