#!/usr/bin/env python3
"""cwi_unpack - restore a CWI container with ZERO data loss.

Default: writes the ORIGINAL source file back, byte-for-byte (from the
ORIG chunk).  --pixels writes a pixel-exact PNG from the decoded PXRL
data instead.  --verify checks the two agree when the original is a
PNG/BMP the tool can read.

Usage:  python3 cwi_unpack.py <in.cwi> [-o out] [--pixels] [--verify]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cwi_common import parse_cwi, read_png_rgba, read_bmp_rgba, write_png_rgba

def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__); return 2
    src = args[0]; out = None; pixels = False; verify = False
    i = 1
    while i < len(args):
        if args[i] == "-o" and i + 1 < len(args): out = args[i + 1]; i += 2
        elif args[i] == "--pixels": pixels = True; i += 1
        elif args[i] == "--verify": verify = True; i += 1
        else: print("bad arg:", args[i]); return 2
    with open(src, "rb") as f:
        cwi = f.read()
    px, w, h, orig, meta = parse_cwi(cwi)
    if out is None:
        name = meta.get("src", os.path.splitext(os.path.basename(src))[0] + ".png")
        out = "restored_" + name
    if pixels or orig is None:
        write_png_rgba(out, px, w, h)
        print("wrote %s (%dx%d PNG from PXRL pixels)" % (out, w, h))
    else:
        with open(out, "wb") as f:
            f.write(orig)
        print("wrote %s (%d bytes, EXACT original file restored)" % (out, len(orig)))
    if verify:
        if orig is None:
            print("--verify: no ORIG chunk; nothing to cross-check"); return 1
        ext = os.path.splitext(meta.get("src", ""))[1].lower()
        try:
            opx, ow, oh = (read_png_rgba if ext == ".png" else read_bmp_rgba)(out)
            if (ow, oh) != (w, h) or opx != px:
                print("VERIFY FAILED: pixels differ from the original"); return 1
            print("verify: pixels match the original image exactly (zero loss)")
        except Exception as e:
            print("verify: could not re-read the original (%s)" % e); return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
