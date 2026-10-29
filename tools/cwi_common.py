"""CWI (CWebView Image) shared codec logic - stdlib only.

Used by cwi_pack.py, cwi_unpack.py and the desktop CWI unit gate, so the
Python encoder and the C decoder (sources/shared/sources/core/cwv_cwi.c) are cross-
validated against each other.  Format: see sources/shared/sources/core/cwv_cwi.h.
"""
import struct, zlib

MAGIC = b"CWIF"
VERSION = 1
FMT_RGBA8 = 0

def _chunks(put):
    def chunk(fourcc, data):
        put(struct.pack("<I", int.from_bytes(fourcc.encode(), "little")))
        put(struct.pack("<I", len(data)))
        put(data)
    return chunk

def pack_cwi(pixels, w, h, orig_bytes=None, src_name=None):
    """pixels: bytes of RGBA8, len == w*h*4. Returns the CWI file bytes."""
    if len(pixels) != w * h * 4:
        raise ValueError("pixel buffer size mismatch")
    if not (0 < w <= 65535 and 0 < h <= 65535):
        raise ValueError("dimensions out of range")
    out = bytearray()
    out += MAGIC
    out += struct.pack("<HHHHI", VERSION, FMT_RGBA8, w, h, 16)
    chunk = _chunks(out.extend)
    chunk("PXRL", rle_encode(pixels))
    if orig_bytes is not None:
        chunk("ORIG", zlib.compress(orig_bytes, 9))
    if src_name:
        chunk("META", ("src=%s\n" % src_name).encode("utf-8", "replace"))
    return bytes(out)

def rle_encode(pixels):
    """Greedy run/raw RLE over 4-byte pixels.  Pure lossless: every
    encoded pixel is byte-identical on decode."""
    out = bytearray()
    n = len(pixels) // 4
    px = pixels
    i = 0
    while i < n:
        # count the run of identical pixels starting at i (max 128)
        run = 1
        while (run < 128 and i + run < n and
               px[(i + run) * 4:(i + run) * 4 + 4] == px[i * 4:i * 4 + 4]):
            run += 1
        if run >= 2:
            out.append(run - 1)                       # bit7 clear = run
            out += px[i * 4:i * 4 + 4]
            i += run
        else:
            # raw span: pixels that are NOT part of a >=2 run (max 128)
            j = i
            while (j < n and j - i < 128):
                if (j + 1 < n and
                        px[(j + 1) * 4:(j + 1) * 4 + 4] == px[j * 4:j * 4 + 4]):
                    break
                j += 1
            if j == i:                                # isolated 1-px "run"
                out.append(0)                          # count 1 run
                out += px[i * 4:i * 4 + 4]
                i += 1
            else:
                out.append(0x80 | (j - i - 1))         # bit7 set = raw
                out += px[i * 4:j * 4]
                i = j
    return bytes(out)

def rle_decode(data, w, h):
    out = bytearray()
    q = 0
    total = w * h
    while q < len(data) and len(out) < total * 4:
        token = data[q]; q += 1
        count = (token & 0x7F) + 1
        if len(out) // 4 + count > total:
            raise ValueError("RLE overruns the image")
        if token & 0x80:
            if q + count * 4 > len(data):
                raise ValueError("truncated raw run")
            out += data[q:q + count * 4]; q += count * 4
        else:
            if q + 4 > len(data):
                raise ValueError("truncated run pixel")
            out += data[q:q + 4] * count; q += 4
    if len(out) != total * 4:
        raise ValueError("RLE does not cover the image exactly")
    return bytes(out)

def parse_cwi(buf):
    """Returns (pixels, w, h, orig_bytes_or_None, meta_dict)."""
    if len(buf) < 16 or buf[:4] != MAGIC:
        raise ValueError("not a CWI container")
    version, fmt, w, h, hdrlen = struct.unpack_from("<HHHHI", buf, 4)
    if version != VERSION or fmt != FMT_RGBA8:
        raise ValueError("unsupported CWI version/format %d/%d" % (version, fmt))
    pixels = None; orig = None; meta = {}
    p = hdrlen
    while p + 8 <= len(buf):
        fourcc = struct.unpack_from("<I", buf, p)[0].to_bytes(4, "little").decode("latin1")
        clen = struct.unpack_from("<I", buf, p + 4)[0]
        if p + 8 + clen > len(buf):
            raise ValueError("chunk %s overruns the file" % fourcc)
        data = buf[p + 8:p + 8 + clen]
        if fourcc == "PXRL":
            pixels = rle_decode(data, w, h)
        elif fourcc == "ORIG":
            orig = zlib.decompress(data)
        elif fourcc == "META":
            for line in data.decode("utf-8", "replace").splitlines():
                if "=" in line:
                    k, v = line.split("=", 1); meta[k.strip()] = v.strip()
        p += 8 + clen
    if pixels is None:
        raise ValueError("no PXRL chunk")
    return pixels, w, h, orig, meta

# ---- pixel source readers (for packing) --------------------------------

def read_png_rgba(path):
    """Minimal lossless PNG reader: 8-bit RGBA/RGB + palette, no interlace.
    Used so cwi_pack has zero third-party dependencies; anything richer
    still packs through the ORIG chunk untouched."""
    with open(path, "rb") as f:
        buf = f.read()
    if buf[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8; idat = bytearray(); plte = None; trns = None
    w = h = bitd = ctype = None
    while pos + 8 <= len(buf):
        (length, typ) = struct.unpack_from(">I4s", buf, pos); pos += 8
        data = buf[pos:pos + length]; pos += length + 4   # + CRC
        if typ == b"IHDR":
            w, h, bitd, ctype, comp, filt, inter = struct.unpack(">IIBBBBB", data)
            if bitd != 8 or inter != 0 or comp != 0:
                raise ValueError("unsupported PNG (need 8-bit, non-interlaced)")
        elif typ == b"PLTE":
            plte = data
        elif typ == b"tRNS":
            trns = data
        elif typ == b"IDAT":
            idat += data
        elif typ == b"IEND":
            break
    if w is None:
        raise ValueError("PNG has no IHDR")
    raw = zlib.decompress(bytes(idat))
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = w * ch
    out = bytearray()
    prev = bytearray(stride)
    q = 0
    for y in range(h):
        f = raw[q]; q += 1
        line = bytearray(raw[q:q + stride]); q += stride
        if f == 1:   # Sub
            for x in range(ch, stride): line[x] = (line[x] + line[x - ch]) & 0xFF
        elif f == 2: # Up
            for x in range(stride): line[x] = (line[x] + prev[x]) & 0xFF
        elif f == 3: # Average
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                line[x] = (line[x] + (a + prev[x]) // 2) & 0xFF
        elif f == 4: # Paeth
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                b = prev[x]; c = prev[x - ch] if x >= ch else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        elif f != 0:
            raise ValueError("bad PNG filter %d" % f)
        out += line
        prev = line
    # to RGBA
    px = bytearray(w * h * 4)
    if ctype == 6:
        return bytes(out[:w * h * 4]), w, h
    i = 0
    for k in range(w * h):
        if ctype == 2:
            r, g, b = out[i], out[i+1], out[i+2]; a = 255; i += 3
        elif ctype == 3:
            idx = out[i]; i += 1
            r, g, b = plte[idx*3], plte[idx*3+1], plte[idx*3+2]
            a = trns[idx] if (trns and idx < len(trns)) else 255
        elif ctype == 0:
            g8 = out[i]; r = g = b = g8; a = 255; i += 1
        elif ctype == 4:
            g8, a = out[i], out[i+1]; r = g = b = g8; i += 2
        else:
            raise ValueError("unsupported PNG color type %d" % ctype)
        px[k*4:k*4+4] = bytes((r, g, b, a))
    return bytes(px), w, h

def read_bmp_rgba(path):
    with open(path, "rb") as f:
        buf = f.read()
    if buf[:2] != b"BM":
        raise ValueError("not a BMP")
    data_off, = struct.unpack_from("<I", buf, 10)
    w, h = struct.unpack_from("<ii", buf, 18)
    bpp, = struct.unpack_from("<H", buf, 28)
    top_down = h < 0
    h = abs(h)
    if bpp not in (24, 32):
        raise ValueError("need 24/32bpp BMP")
    stride = ((w * bpp // 8) + 3) & ~3
    px = bytearray(w * h * 4)
    for y in range(h):
        sy = y if top_down else (h - 1 - y)
        row = buf[data_off + sy * stride:]
        for x in range(w):
            b, g, r = row[x*(bpp//8)], row[x*(bpp//8)+1], row[x*(bpp//8)+2]
            a = row[x*4+3] if bpp == 32 else 255
            px[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((r, g, b, a))
    return bytes(px), w, h

def write_png_rgba(path, px, w, h):
    """Lossless PNG writer (filter 0) - the pixel-faithful unpack output."""
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += px[y * stride:(y + 1) * stride]
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))
