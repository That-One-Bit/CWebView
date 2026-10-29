

(function () {
  'use strict';



  var GD = {

    objects: 'objects',
    sprites: 'sprites',
    sounds: 'sounds',
    rooms: 'rooms',
    texturePages: 'texturePages',
    pageMipFlags: '_88',
    frameRects: 'spriteRects',
    textureGroups: 'textureGroups',
    scriptNames: '_oj',
    scripts: '_pj',

    name: 'name',
    objSprite: 'sprite',
    soundExt: '_n1',
    soundFile: '_o1',
    roomInstances: 'instances',
    roomLayers: 'layers',
    layerInstanceIds: '_f7',
    layerInstanceCount: '_e7',
    layerTileData: '_W6',
    layerTileLen: '_S6',
    layerTileCols: '_T6',
    layerTileRows: '_U6',
    layerSprites: '__6',
    layerSpriteCount: '_Z6',
    instCreationCode: 'creationCode',
    instColor: '_C6',
    groupPages: '_a8',
    groupSprites: 'groupSprites',

    roomRaw: '_l41',
    roomViews: '_H6',

    roomBackgrounds: '_G6',
    roomTiles: '_N6',
    roomBgColor: '_C6',
    roomPersistent: '_E6',
    roomLayerFlag: '_D6',
    roomCreateCode: 'creationCode',
    roomOrderPos: '_p41',
    layerColour: 'backgroundColor',
    roomOrderList: '_Z7'
  };

  var EN = {
    normalizeAssetURL: '_G61',
    makeSoundXhr: 'yyHttpRequest',
    loadImage: '__o',
    loadSoundAsync: '_I81',
    loadSoundSync: '_z81',
    instanceTable: '_kL',
    spriteTable: '_tm',
    imageList: '_xl',
    classRegistry: '_ek',
    classTable: '_G32',
    saveVarMap: '_pM',

    useTimeSource: '_uk',
    timeSource: '_wk',
    timeSourceFps: '_xk',
    roomManager: 'yyCurrentRoom',
    roomSpeedGet: '_tk',

    roomList: 'yyRoomStore',
    roomListArr: '_q41',
    roomListPush: '_Uv',
    roomOrderArr: '_zD2',
    roomClass: '_vD2',
    roomConstants: '_Mb',
    currentRoomFn: 'get_current_room'
  };



  function parseJSONC(text) {
    var out = '', i = 0, textLen = text.length, inString = false, stringQuote = '';
    while (i < textLen) {
      var current = text[i], pair = text.slice(i, i + 2);
      if (inString) {
        out += current;
        if (current === '\\') { out += text[i + 1] || ''; i += 2; continue; }
        if (current === stringQuote) inString = false;
        i++;
        continue;
      }
      if (pair === '//') {
        while (i < textLen && text[i] !== '\n') i++;
        continue;
      }
      if (pair === '/*') {
        var end = text.indexOf('*/', i + 2);
        i = (end === -1) ? textLen : end + 2;
        continue;
      }
      if (current === '"' || current === "'") { inString = true; stringQuote = current; out += current; i++; continue; }
      out += current;
      i++;
    }

    var stripped = '', j = 0, cleanLen = out.length, inString2 = false, quote2 = '';
    while (j < cleanLen) {
      var ch = out[j];
      if (inString2) {
        stripped += ch;
        if (ch === '\\') { stripped += out[j + 1] || ''; j += 2; continue; }
        if (ch === quote2) inString2 = false;
        j++; continue;
      }
      if (ch === '"' || ch === "'") { inString2 = true; quote2 = ch; stripped += ch; j++; continue; }
      if (ch === ',') {
        var scanIdx = j + 1;
        while (scanIdx < cleanLen &&
               (out[scanIdx] === ' ' || out[scanIdx] === '\t' || out[scanIdx] === '\n' || out[scanIdx] === '\r')) scanIdx++;
        if (scanIdx < cleanLen && (out[scanIdx] === ']' || out[scanIdx] === '}')) { j++; continue; }
      }
      stripped += ch; j++;
    }
    return JSON.parse(stripped);
  }

  var CRC_TABLE = (function () {
    var table = new Int32Array(256), crc, bit;
    for (var entry = 0; entry < 256; entry++) {
      crc = entry;
      for (bit = 0; bit < 8; bit++) crc = (crc & 1) ? (0xEDB88320 ^ (crc >>> 1)) : (crc >>> 1);
      table[entry] = crc;
    }
    return table;
  })();
  function crc32(bytes) {
    var crc = -1;
    for (var i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
    return (crc ^ -1) >>> 0;
  }

  function inflate(src, expectedSize) {
    var dst = new Uint8Array(expectedSize || (src.length * 4) + 1024);
    var dstLen = 0;
    var bitBuf = 0, bitCnt = 0, pos = 0;

    var inputDone = false;
    function need(n) {
      while (bitCnt < n) {
        if (pos >= src.length) { inputDone = true; break; }
        bitBuf |= src[pos++] << bitCnt;
        bitCnt += 8;
      }
    }
    function bits(n) { need(n); var value = bitBuf & ((1 << n) - 1); bitBuf >>>= n; bitCnt -= n; return value; }

    function buildHuff(lengths) {

      var counts = new Int32Array(16), i;
      for (i = 0; i < lengths.length; i++) counts[lengths[i]]++;
      counts[0] = 0;
      var next = new Int32Array(16), code = 0;
      for (i = 1; i <= 15; i++) { code = (code + counts[i - 1]) << 1; next[i] = code; }

      var table = new Int32Array(1 << 15), offs = new Int32Array(16);
      for (i = 0; i < 16; i++) offs[i] = next[i];
      for (i = 0; i < lengths.length; i++) {
        var codeLen = lengths[i];
        if (codeLen === 0) continue;
        var code = offs[codeLen];
        offs[codeLen]++;
        var reversedCode = 0;
        for (var bit = 0; bit < codeLen; bit++) reversedCode |= ((code >>> bit) & 1) << (codeLen - 1 - bit);
        var stride = 1 << codeLen, span = 1 << (15 - codeLen), symbol = i;
        for (var spanIdx = 0; spanIdx < span; spanIdx++) table[reversedCode + spanIdx * stride] = symbol | (codeLen << 16);
      }
      return { table: table };
    }
    function decodeSym(huff) {
      need(15);
      var entry = huff.table[bitBuf & 0x7FFF];
      var codeLen = entry >>> 16;
      if (codeLen === 0 || codeLen > 15) throw new Error('inflate: bad code');
      bitBuf >>>= codeLen; bitCnt -= codeLen;
      return entry & 0xFFFF;
    }

    var LEN_BASE = [3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258];
    var LEN_EXTRA = [0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0];
    var DIST_BASE = [1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577];
    var DIST_EXTRA = [0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13];

    function ensure(extra) {
      if (dstLen + extra > dst.length) {
        var bigger = new Uint8Array(Math.max(dst.length * 2, dstLen + extra + 4096));
        bigger.set(dst.subarray(0, dstLen));
        dst = bigger;
      }
    }

    var litHuff, distHuff;

    var fixedLit = null, fixedDist = null;
    function buildFixed() {
      if (fixedLit) return;
      var litLengths = new Uint8Array(288);
      for (var i = 0; i < 144; i++) litLengths[i] = 8;
      for (; i < 256; i++) litLengths[i] = 9;
      for (; i < 280; i++) litLengths[i] = 7;
      for (; i < 288; i++) litLengths[i] = 8;
      var distLengths = new Uint8Array(30);
      for (i = 0; i < 30; i++) distLengths[i] = 5;
      fixedLit = buildHuff(litLengths);
      fixedDist = buildHuff(distLengths);
    }

    function block(hLit, hDist) {
      for (;;) {
        var sym = decodeSym(hLit);
        if (sym === 256) return;
        if (sym < 256) { ensure(1); dst[dstLen++] = sym; }
        else {
          var lenCode = sym - 257;
          if (lenCode < 0 || lenCode > 28) throw new Error('inflate: bad length code');
          var matchLen = LEN_BASE[lenCode] + (LEN_EXTRA[lenCode] ? bits(LEN_EXTRA[lenCode]) : 0);
          var distCode = decodeSym(hDist);
          if (distCode < 0 || distCode > 29) throw new Error('inflate: bad dist code');
          var distance = DIST_BASE[distCode] + (DIST_EXTRA[distCode] ? bits(DIST_EXTRA[distCode]) : 0);
          ensure(matchLen);
          if (distance > dstLen) throw new Error('inflate: distance too far back');
          for (var copyIdx = 0; copyIdx < matchLen; copyIdx++) { dst[dstLen] = dst[dstLen - distance]; dstLen++; }
        }
      }
    }

    while (true) {
      var bfinal = bits(1), btype = bits(2);
      if (btype === 0) {
        bitBuf = 0; bitCnt = 0;
        var len = src[pos] | (src[pos + 1] << 8);
        var nlen = src[pos + 2] | (src[pos + 3] << 8);
        if ((len ^ 0xFFFF) !== nlen) throw new Error('inflate: stored len mismatch');
        pos += 4;
        ensure(len);
        dst.set(src.subarray(pos, pos + len), dstLen);
        dstLen += len; pos += len;
      } else if (btype === 1) {
        buildFixed();
        block(fixedLit, fixedDist);
      } else if (btype === 2) {
        var hlit = bits(5) + 257, hdist = bits(5) + 1, hclen = bits(4) + 4;
        var ORDER = [16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15];
        var headerLens = new Uint8Array(19);
        for (var i = 0; i < hclen; i++) headerLens[ORDER[i]] = bits(3);
        var headerHuff = buildHuff(headerLens);
        var codeLens = new Uint8Array(hlit + hdist);
        var lenIdx = 0;
        while (lenIdx < hlit + hdist) {
          var sym = decodeSym(headerHuff);
          if (sym < 16) codeLens[lenIdx++] = sym;
          else if (sym === 16) {
            if (lenIdx === 0) throw new Error('inflate: repeat with no prev');
            var prev = codeLens[lenIdx - 1], rep = 3 + bits(2);
            while (rep-- && lenIdx < hlit + hdist) codeLens[lenIdx++] = prev;
          } else if (sym === 17) { var rep17 = 3 + bits(3); while (rep17-- && lenIdx < hlit + hdist) codeLens[lenIdx++] = 0; }
          else { var rep18 = 11 + bits(7); while (rep18-- && lenIdx < hlit + hdist) codeLens[lenIdx++] = 0; }
        }
        var dynamicLit = buildHuff(codeLens.subarray(0, hlit));
        var dynamicDist = buildHuff(codeLens.subarray(hlit));
        block(dynamicLit, dynamicDist);
      } else throw new Error('inflate: bad block type');
      if (bfinal) break;
    }
    return dst.subarray(0, dstLen);
  }

  function readZip(buf) {
    var view = new DataView(buf), bytes = new Uint8Array(buf);

    var eocd = -1;
    for (var i = bytes.length - 22; i >= Math.max(0, bytes.length - 22 - 65536); i--) {
      if (bytes[i] === 0x50 && bytes[i + 1] === 0x4B && bytes[i + 2] === 0x05 && bytes[i + 3] === 0x06) { eocd = i; break; }
    }
    if (eocd < 0) throw new Error('zip: EOCD not found');
    var entryCount = view.getUint16(eocd + 10, true);
    var centralDirOffset = view.getUint32(eocd + 16, true);
    var files = {};
    var entryOffset = centralDirOffset;
    for (var entryIndex = 0; entryIndex < entryCount; entryIndex++) {
      if (view.getUint32(entryOffset, true) !== 0x02014B50) throw new Error('zip: bad central directory entry');
      var method = view.getUint16(entryOffset + 10, true);
      var crc = view.getUint32(entryOffset + 16, true) >>> 0;
      var compressedSize = view.getUint32(entryOffset + 20, true);
      var uncompressedSize = view.getUint32(entryOffset + 24, true);
      var nameLen = view.getUint16(entryOffset + 28, true);
      var extraLen = view.getUint16(entryOffset + 30, true);
      var commentLen = view.getUint16(entryOffset + 32, true);
      var localHeaderOffset = view.getUint32(entryOffset + 42, true);
      var name = '';
      for (var nameByteIndex = 0; nameByteIndex < nameLen; nameByteIndex++)
        name += String.fromCharCode(bytes[entryOffset + 46 + nameByteIndex]);

      if (view.getUint32(localHeaderOffset, true) !== 0x04034B50) throw new Error('zip: bad local header');
      var localNameLen = view.getUint16(localHeaderOffset + 26, true);
      var localExtraLen = view.getUint16(localHeaderOffset + 28, true);
      var dataStart = localHeaderOffset + 30 + localNameLen + localExtraLen;
      var compressed = bytes.subarray(dataStart, dataStart + compressedSize);
      var data;
      if (method === 0) data = compressed;
      else if (method === 8) data = inflate(compressed, uncompressedSize);
      else throw new Error('zip: unsupported compression method ' + method);
      if (crc32(data) !== crc) throw new Error('zip: CRC mismatch for ' + name);
      files[name] = data;
      entryOffset += 46 + nameLen + extraLen + commentLen;
    }
    return files;
  }

  var _gameStepCount = 0;
  function _gameSteps() { return _gameStepCount; }

  var _crcTable = null;
  function _crc32(buf) {
    if (!_crcTable) {
      _crcTable = new Uint32Array(256);
      for (var n = 0; n < 256; n++) {
        var c = n;
        for (var k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
        _crcTable[n] = c >>> 0;
      }
    }
    var c2 = 0xFFFFFFFF;
    for (var i = 0; i < buf.length; i++) c2 = _crcTable[(c2 ^ buf[i]) & 0xFF] ^ (c2 >>> 8);
    return (c2 ^ 0xFFFFFFFF) >>> 0;
  }

  function _adler32(buf) {
    var a = 1, b = 0;
    for (var i = 0; i < buf.length; i++) {
      a = (a + buf[i]) % 65521;
      b = (b + a) % 65521;
    }
    return ((b << 16) | a) >>> 0;
  }

  function _pngChunk(type, data) {
    var out = new Uint8Array(12 + data.length);
    out[0] = (data.length >>> 24) & 255; out[1] = (data.length >>> 16) & 255;
    out[2] = (data.length >>> 8) & 255; out[3] = data.length & 255;
    for (var i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
    out.set(data, 8);
    var crcInput = out.subarray(4, 8 + data.length);
    var crc = _crc32(crcInput);
    out[8 + data.length] = (crc >>> 24) & 255; out[9 + data.length] = (crc >>> 16) & 255;
    out[10 + data.length] = (crc >>> 8) & 255; out[11 + data.length] = crc & 255;
    return out;
  }

  function _zlibStored(raw) {
    var blocks = Math.max(1, Math.ceil(raw.length / 65535));
    var out = new Uint8Array(2 + raw.length + blocks * 5 + 4);
    var p = 0;
    out[p++] = 0x78; out[p++] = 0x01;
    for (var b = 0; b < blocks; b++) {
      var start = b * 65535, len = Math.min(65535, raw.length - start);
      var final = (b === blocks - 1) ? 1 : 0;
      out[p++] = final;
      out[p++] = len & 255; out[p++] = (len >>> 8) & 255;
      out[p++] = (~len) & 255; out[p++] = ((~len) >>> 8) & 255;
      if (len) { out.set(raw.subarray(start, start + len), p); p += len; }
    }
    var ad = _adler32(raw);
    out[p++] = (ad >>> 24) & 255; out[p++] = (ad >>> 16) & 255;
    out[p++] = (ad >>> 8) & 255; out[p++] = ad & 255;
    return out.subarray(0, p);
  }

  function _pngEncodeStrip(frames, w, h) {
    var n = frames.length;
    var stride = w * 4;
    var raw = new Uint8Array((stride * n + 1) * h);
    var p = 0;
    for (var y = 0; y < h; y++) {
      raw[p++] = 0;
      for (var f = 0; f < n; f++) {
        raw.set(frames[f].subarray(y * stride, (y + 1) * stride), p);
        p += stride;
      }
    }
    var ihdr = new Uint8Array(13);
    ihdr[0] = (w * n >>> 24) & 255; ihdr[1] = (w * n >>> 16) & 255;
    ihdr[2] = (w * n >>> 8) & 255; ihdr[3] = w * n & 255;
    ihdr[4] = (h >>> 24) & 255; ihdr[5] = (h >>> 16) & 255;
    ihdr[6] = (h >>> 8) & 255; ihdr[7] = h & 255;
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    var sig = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]);
    var c1 = _pngChunk('IHDR', ihdr);
    var c2 = _pngChunk('IDAT', _zlibStored(raw));
    var c3 = _pngChunk('IEND', new Uint8Array(0));
    var out = new Uint8Array(sig.length + c1.length + c2.length + c3.length);
    var q = 0;
    out.set(sig, q); q += sig.length;
    out.set(c1, q); q += c1.length;
    out.set(c2, q); q += c2.length;
    out.set(c3, q);
    return out;
  }

  function _u16le(b, o) { return b[o] | (b[o + 1] << 8); }

  function _decodeGIF(bytes) {
    if (String.fromCharCode(bytes[0], bytes[1], bytes[2]) !== 'GIF')
      throw new Error('animated: bad GIF signature');
    var w = _u16le(bytes, 6), h = _u16le(bytes, 8);
    var packed = bytes[10];
    var p = 13;
    var gct = null;
    if (packed & 0x80) {
      var gctSize = 2 << (packed & 7);
      gct = bytes.subarray(p, p + gctSize * 3);
      p += gctSize * 3;
    }
    var frames = [], delays = [];
    var canvas = new Uint8Array(w * h * 4);
    var saved = null;
    var delayMs = 100, transparent = -1, disposal = 0;
    while (p < bytes.length) {
      var b0 = bytes[p];
      if (b0 === 0x3B) break;
      if (b0 === 0x21) {
        var label = bytes[p + 1];
        p += 2;
        if (label === 0xF9) {
          var gce = bytes[p + 1];
          delayMs = _u16le(bytes, p + 2) * 10;
          if (delayMs <= 0) delayMs = 100;
          transparent = (gce & 1) ? bytes[p + 4] : -1;
          disposal = (gce >> 2) & 7;

        }
        while (bytes[p] !== 0 && p < bytes.length) p += bytes[p] + 1;
        p++;
        continue;
      }
      if (b0 === 0x2C) {
        var fx = _u16le(bytes, p + 1), fy = _u16le(bytes, p + 3);
        var fw = _u16le(bytes, p + 5), fh = _u16le(bytes, p + 7);
        var ipacked = bytes[p + 9];
        p += 10;
        if (ipacked & 0x80) {
          var lctSize = 2 << (ipacked & 7);
          var lct = bytes.subarray(p, p + lctSize * 3);
          p += lctSize * 3;
        } else lct = null;
        var pal = lct || gct;
        if (!pal) throw new Error('animated: GIF frame without a color table');
        var minCode = bytes[p]; p++;
        var lzwData = [];
        while (p < bytes.length && bytes[p] !== 0) {
          lzwData.push(bytes.subarray(p + 1, p + 1 + bytes[p]));
          p += bytes[p] + 1;
        }
        p++;
        var lzw = new Uint8Array(Array.prototype.concat.apply([], lzwData.map(function (a) { return Array.prototype.slice.call(a); })));
        var clear = 1 << minCode, end = clear + 1;
        var codeSize = minCode + 1, next = end + 1;
        var dict = [];
        for (var di = 0; di < clear; di++) dict.push([di]);
        dict.push([null]);
        dict.push([null]);
        var indices = [];
        var bitBuf = 0, bitCnt = 0, lp = 0, prev = -1;
        while (lp < lzw.length || bitCnt >= codeSize) {
          while (bitCnt < codeSize && lp < lzw.length) {
            bitBuf |= lzw[lp++] << bitCnt;
            bitCnt += 8;
          }
          if (bitCnt < codeSize) break;
          var code = bitBuf & ((1 << codeSize) - 1);
          bitBuf >>>= codeSize;
          bitCnt -= codeSize;
          if (code === clear) {
            codeSize = minCode + 1; next = end + 1;
            dict = [];
            for (var d2 = 0; d2 < clear; d2++) dict.push([d2]);
            dict.push([null]);
            dict.push([null]);
            prev = -1;
            continue;
          }
          if (code === end) break;
          var entry;
          if (code < dict.length) entry = dict[code];
          else if (code === dict.length && prev >= 0) entry = dict[prev].concat([dict[prev][0]]);
          else throw new Error('animated: bad GIF LZW code ' + code);
          for (var e2 = 0; e2 < entry.length; e2++) indices.push(entry[e2]);
          if (prev >= 0 && next < 4096) {
            dict.push(dict[prev].concat([entry[0]]));
            next++;
            if (next >= (1 << codeSize) && codeSize < 12) codeSize++;
          }
          prev = code;
          if (indices.length >= fw * fh) break;
        }
        for (var py = 0; py < fh; py++) {
          for (var px = 0; px < fw; px++) {
            var idx2 = indices[py * fw + px];
            if (idx2 === transparent) continue;
            var ci = idx2 * 3;
            var o2 = ((fy + py) * w + (fx + px)) * 4;
            canvas[o2] = pal[ci]; canvas[o2 + 1] = pal[ci + 1];
            canvas[o2 + 2] = pal[ci + 2]; canvas[o2 + 3] = 255;
          }
        }
        frames.push(canvas.slice(0));
        delays.push(delayMs);
        if (disposal === 2) {
          for (var ry = 0; ry < fh; ry++)
            for (var rx = 0; rx < fw; rx++) {
              var o3 = ((fy + ry) * w + (fx + rx)) * 4;
              canvas[o3] = canvas[o3 + 1] = canvas[o3 + 2] = canvas[o3 + 3] = 0;
            }
        } else if (disposal === 3 && saved) {
          canvas.set(saved);
        }
        if (disposal === 3) saved = canvas.slice(0);
        else if (frames.length === 1) saved = canvas.slice(0);
        transparent = -1; disposal = 0;
        continue;
      }
      p++;
    }
    if (!frames.length) throw new Error('animated: GIF has no frames');
    return { format: 'gif', w: w, h: h, frames: frames, delays: delays };
  }

  function _apngUnfilter(raw, w, h) {
    var bpp = 4, stride = w * bpp;
    var out = new Uint8Array(stride * h);
    var p = 0;
    for (var y = 0; y < h; y++) {
      var ft = raw[p++];
      var row = y * stride, up = (y > 0) ? row - stride : -1;
      for (var x = 0; x < stride; x++) {
        var a = (x >= bpp) ? out[row + x - bpp] : 0;
        var b = (up >= 0) ? out[up + x] : 0;
        var c2 = (up >= 0 && x >= bpp) ? out[up + x - bpp] : 0;
        var v = raw[p++];
        if (ft === 1) v = (v + a) & 255;
        else if (ft === 2) v = (v + b) & 255;
        else if (ft === 3) v = (v + ((a + b) >> 1)) & 255;
        else if (ft === 4) {
          var pa = Math.abs(b - c2), pb = Math.abs(a - c2), pc = Math.abs(a + b - 2 * c2);
          var pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c2);
          v = (v + pr) & 255;
        } else if (ft !== 0) throw new Error('animated: bad PNG filter type ' + ft);
        out[row + x] = v;
      }
    }
    return out;
  }

  function _decodeAPNG(bytes) {
    if (bytes.length < 24 || String.fromCharCode(bytes[1], bytes[2], bytes[3]) !== 'PNG')
      throw new Error('animated: bad PNG signature');
    var p = 8, w = 0, h = 0, idatChunks = [], apngCt = 6;
    var actl = null, frameChunks = [], fcBeforeIDAT = false, seenIDAT = false;
    while (p + 8 <= bytes.length) {
      var len = (bytes[p] << 24 >>> 0) + (bytes[p + 1] << 16) + (bytes[p + 2] << 8) + bytes[p + 3];
      var type = String.fromCharCode(bytes[p + 4], bytes[p + 5], bytes[p + 6], bytes[p + 7]);
      var data = bytes.subarray(p + 8, p + 8 + len);
      if (type === 'IHDR') {
        w = (data[0] << 24 >>> 0) + (data[1] << 16) + (data[2] << 8) + data[3];
        h = (data[4] << 24 >>> 0) + (data[5] << 16) + (data[6] << 8) + data[7];
        apngCt = data[9];
      }
      else if (type === 'acTL') actl = data;
      else if (type === 'fcTL') { if (!seenIDAT) fcBeforeIDAT = true; frameChunks.push({ fc: data, dat: null }); }
      else if (type === 'fdAT') {
        if (frameChunks.length) {
          var fcLast = frameChunks[frameChunks.length - 1];
          if (!fcLast.dat) fcLast.dat = [data];
          else fcLast.dat.push(data);
        }
      }
      else if (type === 'IDAT') { idatChunks.push(data); seenIDAT = true; }
      p += 12 + len;
    }
    if (!actl) return null;
    if (apngCt !== 6) throw new Error('animated: APNG must be 8-bit RGBA (colortype 6)');
    var frames = [], delays = [];
    var canvas = new Uint8Array(w * h * 4);
    var saved = null;
    var entries = [];
    if (fcBeforeIDAT) {
      entries.push({ fc: frameChunks[0].fc, dat: idatChunks, idat: true });
      entries = entries.concat(frameChunks.slice(1));
    } else {
      entries.push({ fc: null, dat: idatChunks, idat: true });
      entries = entries.concat(frameChunks);
    }
    for (var fi = 0; fi < entries.length; fi++) {
      var e = entries[fi];
      var fc = e.fc;
      var fw = w, fh = h, fx = 0, fy = 0, dms = 100, dop = 0, bop = 0;
      if (fc) {
        fw = (fc[4] << 24 >>> 0) + (fc[5] << 16) + (fc[6] << 8) + fc[7];
        fh = (fc[8] << 24 >>> 0) + (fc[9] << 16) + (fc[10] << 8) + fc[11];
        fx = (fc[12] << 24 >>> 0) + (fc[13] << 16) + (fc[14] << 8) + fc[15];
        fy = (fc[16] << 24 >>> 0) + (fc[17] << 16) + (fc[18] << 8) + fc[19];
        var dnum = (fc[20] << 8) + fc[21], dden = (fc[22] << 8) + fc[23];
        dms = dden ? Math.round(dnum * 1000 / dden) : 100;
        if (dms <= 0) dms = 100;
        dop = fc[25] & 7;
        bop = fc[24] & 7;
      }
      var parts = e.dat;
      var stripPrefix = !e.idat;
      var catLen = 0;
      for (var pi2 = 0; pi2 < parts.length; pi2++) catLen += parts[pi2].length - (stripPrefix ? 4 : 0);
      var cat = new Uint8Array(catLen);
      var cp = 0;
      for (var pi3 = 0; pi3 < parts.length; pi3++) {
        cat.set(parts[pi3].subarray(stripPrefix ? 4 : 0), cp);
        cp += parts[pi3].length - (stripPrefix ? 4 : 0);
      }
      if (cat[0] !== 0x78) throw new Error('animated: PNG frame is not zlib-wrapped');
      var raw = inflate(cat.subarray(2), (fw * 4 + 1) * fh);
      var img = _apngUnfilter(raw, fw, fh);
      for (var ry2 = 0; ry2 < fh; ry2++) {
        for (var rx2 = 0; rx2 < fw; rx2++) {
          var src = (ry2 * fw + rx2) * 4;
          var dst = ((fy + ry2) * w + (fx + rx2)) * 4;
          if (bop === 0 || img[src + 3] === 255) {
            canvas[dst] = img[src]; canvas[dst + 1] = img[src + 1];
            canvas[dst + 2] = img[src + 2]; canvas[dst + 3] = img[src + 3];
          } else if (img[src + 3] > 0) {
            var sa = img[src + 3] / 255, da = canvas[dst + 3] / 255;
            canvas[dst] = Math.round(img[src] * sa + canvas[dst] * (1 - sa));
            canvas[dst + 1] = Math.round(img[src + 1] * sa + canvas[dst + 1] * (1 - sa));
            canvas[dst + 2] = Math.round(img[src + 2] * sa + canvas[dst + 2] * (1 - sa));
            canvas[dst + 3] = Math.round(255 * (sa + da * (1 - sa)));
          }
        }
      }
      frames.push(canvas.slice(0));
      delays.push(dms);
      if (dop === 1) {
        for (var ry3 = 0; ry3 < fh; ry3++)
          for (var rx3 = 0; rx3 < fw; rx3++) {
            var o4 = ((fy + ry3) * w + (fx + rx3)) * 4;
            canvas[o4] = canvas[o4 + 1] = canvas[o4 + 2] = canvas[o4 + 3] = 0;
          }
      } else if (dop === 2 && saved) {
        canvas.set(saved);
      }
      saved = frames[frames.length - 1].slice(0);
    }
    return { format: 'apng', w: w, h: h, frames: frames, delays: delays };
  }

  function _decodeBMP(bytes) {
    if (bytes[0] !== 0x42 || bytes[1] !== 0x4D)
      throw new Error('animated: bad BMP signature');
    var off = (bytes[10]) | (bytes[11] << 8) | (bytes[12] << 16) | (bytes[13] << 24 >>> 0);
    var w = bytes[18] | (bytes[19] << 8) | (bytes[20] << 16) | (bytes[21] << 24);
    var hSigned = bytes[22] | (bytes[23] << 8) | (bytes[24] << 16) | (bytes[25] << 24);
    var bpp = bytes[28] | (bytes[29] << 8);
    var comp = bytes[30] | (bytes[31] << 8);
    if (bpp !== 24 && bpp !== 32) throw new Error('animated: BMP must be 24 or 32 bpp (got ' + bpp + ')');
    if (comp !== 0) throw new Error('animated: BMP must be uncompressed BI_RGB');
    var h = Math.abs(hSigned), topDown = hSigned < 0;
    var rowSize = Math.floor((bpp * w + 31) / 32) * 4;
    var frame = new Uint8Array(w * h * 4);
    for (var y = 0; y < h; y++) {
      var srcY = topDown ? y : (h - 1 - y);
      for (var x = 0; x < w; x++) {
        var s = off + srcY * rowSize + x * (bpp / 8);
        var d = (y * w + x) * 4;
        frame[d] = bytes[s + 2]; frame[d + 1] = bytes[s + 1]; frame[d + 2] = bytes[s];
        frame[d + 3] = (bpp === 32) ? bytes[s + 3] : 255;
      }
    }
    return { format: 'bmp', w: w, h: h, frames: [frame], delays: [0] };
  }

  function _decodeAnimated(bytes) {
    var b = bytes;
    var info = null;
    if (b[0] === 0x47 && b[1] === 0x49 && b[2] === 0x46) info = _decodeGIF(b);
    else if (b[0] === 0x42 && b[1] === 0x4D) info = _decodeBMP(b);
    else if (b[0] === 137 && b[1] === 80 && b[2] === 78 && b[3] === 71) info = _decodeAPNG(b);
    if (!info || !info.frames.length) return null;
    var del = [];
    for (var i = 0; i < info.delays.length; i++) del.push(info.delays[i] > 0 ? info.delays[i] : 100);
    var strip = _pngEncodeStrip(info.frames, info.w, info.h);
    return {
      format: info.format, strip: strip, frames: info.frames.length,
      frameWidth: info.w, frameHeight: info.h, delays: del
    };
  }



  var gamedata = null;
  var loadedMods = [];
  var allMods = [];
  var redirectMap = {};
  var blobCache = {};
  var startHooks = [];
  var waitTimers = [];
  var waitSeq = 0;
  var bootLog = [];

  var _nextInstanceId = 1000000;

  var _nextRoomInstId = 5000000;
  var roomHooks = { start: [], end: [] };
  var stockCounts = null;

  var DISABLED_KEY = 'modkit.disabled';
  var menuBackground = null;
  var _kitApi = null;

  function setMenuBackgroundInternal(spriteIndex, opts) {
    var spr = gamedata && gamedata[GD.sprites] ? gamedata[GD.sprites][spriteIndex] : null;
    if (!spr) throw new Error('setMenuBackground: unknown sprite index ' + spriteIndex);
    opts = opts || {};
    menuBackground = {
      index: spriteIndex,
      w: spr.width || 1, h: spr.height || 1,
      alpha: opts.alpha !== undefined ? opts.alpha : 1,
      fit: opts.fit === 'cover' ? 'cover' : 'stretch',
      dim: opts.dim !== undefined ? opts.dim : 0.55
    };
    return menuBackground;
  }

  function _getKitAPI() {
    if (!_kitApi) _kitApi = makeAPI({ name: 'ModKit', title: 'ModKit', version: '1' }, {});
    return _kitApi;
  }

  // bootLog keeps the kit's own lines for ModKit.bootLog(); it is capped so a message repeated every frame
  // cannot grow it (or the console) without bound
  var BOOTLOG_MAX = 3000;
  function _bootPush(line) {
    bootLog.push(line);
    if (bootLog.length > BOOTLOG_MAX) bootLog.splice(0, bootLog.length - BOOTLOG_MAX);
  }
  function log() {
    var args = Array.prototype.slice.call(arguments);
    if (typeof console !== 'undefined') console.log.apply(console, args);
    _bootPush(args.join(' '));
  }

  // errors that fire every frame (a failing draw hook, a bad zone action) are reported 3 times, then silenced
  var _loudCounts = {};
  function loud(what, err) {
    var msg = '[modkit] ' + what + (err ? (': ' + (err && (err.stack || err.message || String(err)))) : '');
    var key = what + '|' + (err && (err.message || String(err)));
    var n = _loudCounts[key] = (_loudCounts[key] || 0) + 1;
    if (n > 3) return msg;
    if (n === 3) msg += '\n[modkit] (the same error keeps repeating - further copies are suppressed)';
    try { if (typeof console !== 'undefined' && console.error) console.error(msg); } catch (e) {}
    _bootPush(msg);
    return msg;
  }

  function base64ToBytes(b64) {
    b64 = b64.replace(/[^A-Za-z0-9+\/=]/g, '');
    if (typeof atob === 'function') {
      var decoded = atob(b64), bytes = new Uint8Array(decoded.length);
      for (var i = 0; i < decoded.length; i++) bytes[i] = decoded.charCodeAt(i);
      return bytes;
    }

    var out = [], acc = 0, bitsHeld = 0;
    var B64_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    for (i = 0; i < b64.length; i++) {
      if (b64[i] === '=') break;
      var digitValue = B64_CHARS.indexOf(b64[i]); if (digitValue < 0) continue;
      acc = (acc << 6) | digitValue; bitsHeld += 6;
      if (bitsHeld >= 8) { bitsHeld -= 8; out.push((acc >> bitsHeld) & 0xFF); }
    }
    return new Uint8Array(out);
  }

  function decodeText(raw) {
    if (typeof TextDecoder !== 'undefined') {
      try { return new TextDecoder('utf-8').decode(raw); } catch (e) {}
    }
    var s = '';
    for (var i = 0; i < raw.length; ) {
      var b = raw[i], c;
      if (b < 0x80) { s += String.fromCharCode(b); i++; }
      else if (b < 0xC0) { s += '\uFFFD'; i++; }
      else if (b < 0xE0) { c = ((b & 0x1F) << 6) | (raw[i + 1] & 0x3F); s += String.fromCharCode(c); i += 2; }
      else if (b < 0xF0) { c = ((b & 0x0F) << 12) | ((raw[i + 1] & 0x3F) << 6) | (raw[i + 2] & 0x3F); s += String.fromCharCode(c); i += 3; }
      else {
        c = ((b & 0x07) << 18) | ((raw[i + 1] & 0x3F) << 12) | ((raw[i + 2] & 0x3F) << 6) | (raw[i + 3] & 0x3F);
        c -= 0x10000;
        s += String.fromCharCode(0xD800 + (c >> 10), 0xDC00 + (c & 0x3FF)); i += 4;
      }
    }
    return s;
  }

  // -------------------------------------------------------------------
  // MOD SETTINGS - per-mod entries surfaced in the title-screen menu
  // (Mods > <mod line> > Z). Entries are registered from init.js via
  // ModKit.settings.add; toggle/number values persist in localStorage (key "modkit_settings", see below).
  // Only callbacks the mod itself registered can ever run from the
  // menu - the menu never evaluates strings or invents calls.
  var _settingsRegistry = [];
  function _settingKey(mod, id) {
    return String(mod).replace(/[^A-Za-z0-9]+/g, '_') + '_' + String(id).replace(/[^A-Za-z0-9]+/g, '_');
  }
  // persistence uses localStorage (modkit_settings) - the engine's ini
  // subsystem is not initialized during mod load, and localStorage is
  // available both pre-boot and in the devtools console
  var _settingsStore = null;
  function _settingsLoadStore() {
    if (_settingsStore === null) {
      _settingsStore = {};
      try {
        var raw = window.localStorage.getItem('modkit_settings');
        if (raw) {
          var v = JSON.parse(raw);
          if (v && typeof v === 'object') _settingsStore = v;
        }
      } catch (e) { }
    }
    return _settingsStore;
  }
  function _readSettingIni(key, fallback) {
    var st = _settingsLoadStore();
    return (key in st && typeof st[key] === 'number') ? st[key] : fallback;
  }
  function _writeSettingIni(key, value) {
    var st = _settingsLoadStore();
    st[key] = value;
    try { window.localStorage.setItem('modkit_settings', JSON.stringify(st)); } catch (e) { }
  }
  function _settingsForMod(modName) {
    var out = [];
    for (var i = 0; i < _settingsRegistry.length; i++)
      if (_settingsRegistry[i].mod === modName) out.push(_settingsRegistry[i]);
    return out;
  }
  function _registerModSetting(manifest, opts) {
    if (!opts || typeof opts !== 'object') throw new Error('settings.add: options object required');
    if (!opts.id || typeof opts.id !== 'string') throw new Error('settings.add: id (string) required');
    if (!opts.label || typeof opts.label !== 'string') throw new Error('settings.add: label (string) required');
    var type = opts.type || 'toggle';
    if (type !== 'toggle' && type !== 'action' && type !== 'number')
      throw new Error('settings.add: type must be "toggle", "number" or "action"');
    if (typeof opts.apply !== 'function')
      throw new Error('settings.add: apply callback required (receives the new value; actions get none)');
    for (var i = 0; i < _settingsRegistry.length; i++)
      if (_settingsRegistry[i].mod === manifest.name && _settingsRegistry[i].id === opts.id)
        throw new Error('settings.add: duplicate id "' + opts.id + '" in ' + manifest.name);
    var entry = {
      mod: manifest.name, id: opts.id, label: opts.label, type: type,
      onApply: opts.apply,
      value: (type === 'action') ? null
        : (type === 'toggle') ? !(opts.value === false || opts.value === 0)
        : (Number(opts.value) || 0),
      min: (opts.min === undefined) ? 0 : Number(opts.min),
      max: (opts.max === undefined) ? 99 : Number(opts.max),
      step: (opts.step === undefined) ? 1 : Number(opts.step),
      persist: (type !== 'action') && (opts.persist !== false),
      key: _settingKey(manifest.name, opts.id)
    };
    if (entry.persist) {
      var saved = _readSettingIni(entry.key, null);
      if (saved !== null && isFinite(saved))
        entry.value = (type === 'toggle') ? (saved >= 0.5) : Number(saved);
    }
    _settingsRegistry.push(entry);
    try {
      if (type === 'action') entry.onApply();
      else entry.onApply(entry.value);
    } catch (err) { loud('settings.add: apply failed for ' + manifest.name + '.' + opts.id, err); }
    log('[modkit] ' + manifest.name + ': setting "' + opts.id + '" (' + type + ') registered');
    return true;
  }
  function _settingToggle(entry) {
    entry.value = !entry.value;
    if (entry.persist) _writeSettingIni(entry.key, entry.value ? 1 : 0);
    try { entry.onApply(entry.value); }
    catch (err) { loud('settings: ' + entry.mod + '.' + entry.id + ' apply failed', err); }
  }
  function _settingAdjust(entry, dir) {
    var n = entry.value + dir * (entry.step || 1);
    if (n > entry.max) n = entry.min;
    if (n < entry.min) n = entry.max;
    if (n === entry.value) return;
    entry.value = n;
    if (entry.persist) _writeSettingIni(entry.key, n);
    try { entry.onApply(n); }
    catch (err) { loud('settings: ' + entry.mod + '.' + entry.id + ' apply failed', err); }
  }
  function _settingRun(entry) {
    try { entry.onApply(); }
    catch (err) { loud('settings: ' + entry.mod + '.' + entry.id + ' failed', err); }
  }

  function getDisabled() {
    try {
      var raw = (typeof localStorage !== 'undefined') ? localStorage.getItem(DISABLED_KEY) : null;
      var arr = raw ? JSON.parse(raw) : [];
      return (arr instanceof Array) ? arr : [];
    } catch (e) { return []; }
  }
  function setDisabled(arr) {
    try { if (typeof localStorage !== 'undefined') localStorage.setItem(DISABLED_KEY, JSON.stringify(arr)); } catch (e) {}
  }

  function findIdx(list, name) {
    for (var i = 0; i < list.length; i++)
      if (list[i] && list[i][GD.name] === name) return i;
    return -1;
  }

  function scriptIdx(name) { return gamedata[GD.scriptNames].indexOf(name); }
  function objIdx(name) { var i = findIdx(gamedata[GD.objects], name); if (i < 0) throw new Error('no object named ' + name); return i; }
  function sprIdx(name) { var i = findIdx(gamedata[GD.sprites], name); if (i < 0) throw new Error('no sprite named ' + name); return i; }
  function sndIdx(name) { var i = findIdx(gamedata[GD.sounds], name); if (i < 0) throw new Error('no sound named ' + name); return i; }
  function roomIdx(name) { var i = findIdx(gamedata[GD.rooms], name); if (i < 0) throw new Error('no room named ' + name); return i; }

  var fnWraps = [];

  function _nameAt(table, index, what) {
    var i = typeof index === 'number' ? Math.floor(index) : -1;
    if (i < 0 || i >= table.length) throw new Error(what + ' index out of range: ' + index);
    var nm = table[i] && table[i][GD.name];
    if (typeof nm !== 'string') throw new Error(what + ' index ' + i + ' has no name');
    return nm;
  }

  function _listTable(table, query, mapFn) {
    var q = (query === undefined || query === null) ? '' : String(query).toLowerCase();
    var out = [];
    for (var i = 0; i < table.length; i++) {
      var e = table[i], nm = e && e[GD.name];
      if (typeof nm !== 'string') continue;
      if (q && nm.toLowerCase().indexOf(q) < 0) continue;
      out.push(mapFn(i, e, nm));
    }
    return out;
  }

  function _spriteFrames(e) {
    var fr = e.sequence;
    if (!fr) return 1;
    if (Array.isArray(fr)) return fr.length || 1;
    if (Array.isArray(fr._L1))
      for (var k = 0; k < fr._L1.length; k++)
        if (fr._L1[k] && fr._L1[k]._M1 === 'GMSpriteFramesTrack' && Array.isArray(fr._L1[k]._K1))
          return fr._L1[k]._K1.length || 1;
    return 1;
  }
  function _spriteRow(i, e, nm) {
    return { index: i, name: nm, width: e.width, height: e.height,
             frames: _spriteFrames(e) };
  }
  function _objectRow(i, e, nm) {
    var sp = e[GD.objSprite], sn = null;
    if (sp !== undefined && sp !== null && sp >= 0 && gamedata[GD.sprites][sp])
      sn = gamedata[GD.sprites][sp][GD.name];
    var evs = [];
    for (var k in e) if (e.hasOwnProperty(k) && k.indexOf('ev_') === 0) evs.push(k.slice(3));
    return { index: i, name: nm, sprite: sp, spriteName: sn, events: evs };
  }
  function _soundRow(i, e, nm) { return { index: i, name: nm, volume: e.volume }; }
  function _roomRow(i, e, nm) { return { index: i, name: nm, width: e.width, height: e.height }; }

  function clonePlain(v) {
    if (v === null || typeof v !== 'object') return v;
    if (typeof v === 'function') return undefined;
    if (Array.isArray(v)) { var a = []; for (var i = 0; i < v.length; i++) a.push(clonePlain(v[i])); return a; }
    var o = {};
    for (var k in v) if (v.hasOwnProperty(k) && typeof v[k] !== 'function') o[k] = clonePlain(v[k]);
    return o;
  }

  function _cloneViews(templateViews, width, height) {
    var views = clonePlain(templateViews) || [];
    for (var i = 0; i < views.length; i++) {
      if (i === 0 && views[i]) { views[i]._I6 = width; views[i]._J6 = height; }
    }
    return views;
  }

  // ---- room.remove / room.removeInstance support ------------------------
  function _liveRoomId() {
    try {
      var rmL = window[EN.roomConstants];
      if (rmL && rmL[EN.currentRoomFn]) {
        var v = rmL[EN.currentRoomFn]();
        return (typeof v === 'number') ? v : null;
      }
    } catch (e) { }
    return null;
  }
  function _resolveRoomRef(ref, who) {
    var id = (typeof ref === 'number') ? ref : (typeof ref === 'string' ? roomIdx(ref) : -1);
    if (!(id >= 0 && id < gamedata[GD.rooms].length) || Math.floor(id) !== id)
      throw new Error(who + ': room must be a room id (number) or a room name (string)');
    var rec = gamedata[GD.rooms][id];
    if (rec && rec.__modkitRemoved) throw new Error(who + ': room ' + id + ' was removed');
    return id;
  }
  var _pendingRoomRemovals = [];
  // Retire a mod-added room. The slot stays (room ids are table indexes, other
  // ids must not shift) but the room is emptied and un-nameable, and anything
  // aimed at it (forceNext routes, clues) is dropped.
  function _retireRoom(id) {
    var rec = gamedata[GD.rooms][id];
    var oldName = rec[GD.name];
    rec[GD.name] = '__removed_' + id + '_' + oldName;
    rec[GD.roomInstances] = [];
    var layers = rec[GD.roomLayers] || [];
    for (var li = 0; li < layers.length; li++) {
      if (layers[li][GD.layerInstanceIds]) {
        layers[li][GD.layerInstanceIds] = [];
        layers[li][GD.layerInstanceCount] = 0;
      }
    }
    rec[GD.roomTiles] = [];
    rec.__modkitRemoved = true;
    for (var fi = forcedRooms.length - 1; fi >= 0; fi--)
      if (forcedRooms[fi].roomId === id) forcedRooms.splice(fi, 1);
    for (var cn in clueRegistry) if (clueRegistry.hasOwnProperty(cn) && clueRegistry[cn].room === id) {
      var cs = clueRegistry[cn];
      try { if (cs.inst !== null && instance_exists(cs.inst)) instance_destroy(resolveInstance(-4, -4, cs.inst)); } catch (e) { }
      delete clueRegistry[cn];
    }
    log('[modkit] room ' + id + ' (' + oldName + ') removed');
  }
  function _runPendingRoomRemovals() {
    if (!_pendingRoomRemovals.length) return;
    var live = _liveRoomId();
    for (var i = _pendingRoomRemovals.length - 1; i >= 0; i--) {
      var rid = _pendingRoomRemovals[i];
      if (rid === live) continue;
      _pendingRoomRemovals.splice(i, 1);
      if (gamedata[GD.rooms][rid] && !gamedata[GD.rooms][rid].__modkitRemoved) _retireRoom(rid);
    }
  }

  function _roomHookFits(match, roomId, roomName) {
    if (match === undefined || match === '*') return true;
    if (typeof match === 'number') return match === roomId;
    return match === roomName;
  }
  function _dispatchRoomHooks(kind) {
    if (kind === 'start') _runPendingRoomRemovals();
    var list = roomHooks[kind];
    if (!list.length) return;
    var roomList = window[EN.roomList];
    var roomId = window[EN.roomConstants][EN.currentRoomFn]();
    var roomName = roomList[EN.roomListArr][roomId] ? roomList[EN.roomListArr][roomId][GD.roomRaw][GD.name] : '';
    for (var i = 0; i < list.length; i++) {
      if (!_roomHookFits(list[i].match, roomId, roomName)) continue;
      try { list[i].fn(roomId, roomName); }
      catch (err) { loud('onRoom' + (kind === 'start' ? 'Start' : 'End') + ' hook failed', err); }
    }
  }
  function installRoomEventHooks() {
    var controllerIdx = findIdx(gamedata[GD.objects], 'obj_Controller');
    if (controllerIdx < 0) { log('[modkit] WARNING: obj_Controller not found; room hooks will not run'); return; }
    var entry = gamedata[GD.objects][controllerIdx];
    var origStart = entry.ev_Other4_RoomStart || null;
    var origEnd = entry.ev_Other5_RoomEnd || null;
    entry.ev_Other4_RoomStart = function (self, other) {
      if (origStart) origStart(self, other);
      _dispatchRoomHooks('start');
    };
    entry.ev_Other5_RoomEnd = function (self, other) {
      _dispatchRoomHooks('end');
      if (origEnd) origEnd(self, other);
    };
  }
  function captureStockCounts() {
    stockCounts = {
      objects: gamedata[GD.objects].length,
      sprites: gamedata[GD.sprites].length,
      sounds: gamedata[GD.sounds].length,
      rooms: gamedata[GD.rooms].length,
      scripts: gamedata[GD.scriptNames].length
    };
  }
  function _isModRow(kind, index) {
    return !stockCounts || index >= stockCounts[kind];
  }

  function fnSource(f) {
    if (f === null || f === undefined) return null;
    if (typeof f !== 'function') return String(f);
    var src = String(f);
    var nl = src.indexOf('\n');
    return (nl >= 0 ? src.slice(0, nl + 1) + '  ... }' : src);
  }
  function _describeRoom(id) {
    var roomList = window[EN.roomList];
    var record = gamedata[GD.rooms][id];
    if (!record) throw new Error('no room with id ' + id);
    var liveArr = (roomList && roomList[EN.roomListArr]) ? roomList[EN.roomListArr] : null;
    var obj = liveArr ? (liveArr[id] || null) : null;
    var out = {
      kind: 'room', id: id, name: record[GD.name],
      mod: _isModRow('rooms', id),
      width: record.width, height: record.height,
      speed: record.speed,
      persistent: !!record[GD.roomPersistent],
      background: record[GD.roomBgColor],
      orderPos: obj ? obj[GD.roomOrderPos] : null,
      views: record[GD.roomViews] || [],
      layers: [],
      instances: [],
      tiles: (record[GD.roomTiles] || []).length,
      creationCode: fnSource(record[GD.roomCreateCode]),
      raw: record
    };
    var layers = record[GD.roomLayers] || [];
    for (var i = 0; i < layers.length; i++) out.layers.push({
      name: layers[i][GD.name], type: layers[i].type, depth: layers[i].depth,
      visible: layers[i].visible,
      instanceIds: layers[i][GD.layerInstanceIds] || []
    });
    var insts = record[GD.roomInstances] || [];
    for (var j = 0; j < insts.length; j++) {
      var e = gamedata[GD.objects][insts[j].index];
      out.instances.push({
        id: insts[j].id, object: e ? e[GD.name] : ('#' + insts[j].index),
        x: insts[j].x, y: insts[j].y, rotation: insts[j].rotation,
        scaleX: insts[j].scaleX, scaleY: insts[j].scaleY,
        imageIndex: insts[j].imageIndex, imageSpeed: insts[j].imageSpeed,
        creationCode: fnSource(insts[j][GD.instCreationCode])
      });
    }
    return out;
  }
  function _describeObject(index) {
    var e = gamedata[GD.objects][index];
    if (!e) throw new Error('no object with id ' + index);
    var out = {
      kind: 'object', id: index, name: e[GD.name],
      mod: _isModRow('objects', index),
      visible: e.visible, parent: e.parent,
      sprite: e[GD.objSprite] >= 0 && gamedata[GD.sprites][e[GD.objSprite]]
        ? gamedata[GD.sprites][e[GD.objSprite]][GD.name] : null,
      events: {}, raw: e
    };
    for (var k in e) if (e.hasOwnProperty(k) && k.indexOf('ev_') === 0)
      out.events[k.slice(3)] = fnSource(e[k]);
    return out;
  }
  function _describeSprite(index) {
    var e = gamedata[GD.sprites][index];
    if (!e) throw new Error('no sprite with id ' + index);
    return {
      kind: 'sprite', id: index, name: e[GD.name],
      mod: _isModRow('sprites', index),
      frames: (e.rectIds || []).length, xoffset: e.originX, yoffset: e.originY,
      width: e.width, height: e.height, raw: e
    };
  }
  function _describeSound(index) {
    var e = gamedata[GD.sounds][index];
    if (!e) throw new Error('no sound with id ' + index);
    return {
      kind: 'sound', id: index, name: e[GD.name],
      mod: _isModRow('sounds', index),
      file: e[GD.soundFile], extension: e[GD.soundExt], raw: e
    };
  }
  function _describeScript(index) {
    return {
      kind: 'script', id: index,
      name: gamedata[GD.scriptNames][index],
      mod: _isModRow('scripts', index),
      source: fnSource(gamedata[GD.scripts][index])
    };
  }
  function _describe(target, kind) {
    if (kind) {
      kind = kind.toLowerCase();
      var i = (typeof target === 'number') ? target : (kind === 'scripts' ? scriptIdx(String(target)) : findIdx(gamedata[GD[kind]], String(target)));
      if (i < 0) throw new Error('describe: no ' + kind + ' named ' + target);
      if (kind === 'rooms') return _describeRoom(i);
      if (kind === 'objects') return _describeObject(i);
      if (kind === 'sprites') return _describeSprite(i);
      if (kind === 'sounds') return _describeSound(i);
      return _describeScript(i);
    }
    if (target === undefined || target === null) {
      var rm = window[EN.roomConstants];
      if (!rm || !rm[EN.currentRoomFn])
        throw new Error('describe: the game has not started yet (no current room); describe("rm_First") works at init time');
      var cur = null;
      try { cur = rm[EN.currentRoomFn](); }
      catch (e) { cur = null; }
      if (cur === null || cur === undefined)
        throw new Error('describe: no current room yet (the game has not opened a room); describe("rm_First") works at init time');
      return _describeRoom(cur);
    }
    if (typeof target === 'number') throw new Error('describe: numeric id needs a kind (e.g. describe(26, "rooms"))');
    var tries = [
      [GD.objects, _describeObject], [GD.sprites, _describeSprite],
      [GD.sounds, _describeSound], [GD.rooms, _describeRoom], [GD.scriptNames, _describeScript]
    ];
    for (var ti = 0; ti < tries.length; ti++) {
      var idx = (tries[ti][1] === _describeScript) ? scriptIdx(target)
        : findIdx(gamedata[tries[ti][0]], target);
      if (idx >= 0) return tries[ti][1](idx);
    }
    throw new Error('describe: no resource named ' + target);
  }

  function _resources(kind) {
    var kinds = (kind === 'all' || !kind)
      ? ['objects', 'sprites', 'sounds', 'rooms', 'scripts']
      : [kind];
    var out = [];
    for (var ki = 0; ki < kinds.length; ki++) {
      var k = kinds[ki];
      var table = (k === 'scripts') ? gamedata[GD.scriptNames] : gamedata[GD[k]];
      for (var i = 0; i < table.length; i++) {
        if (table[i] === null) continue;
        out.push({
          kind: k.slice(0, -1), id: i,
          name: (k === 'scripts') ? table[i] : table[i][GD.name],
          mod: _isModRow(k, i)
        });
      }
    }
    return out;
  }

  function _tables() {
    return {
      objects: gamedata[GD.objects], sprites: gamedata[GD.sprites],
      sounds: gamedata[GD.sounds], rooms: gamedata[GD.rooms],
      scripts: gamedata[GD.scripts]
    };
  }

  var dialog = null;
  var _dialogObjIdx = -1;
  function _registerDialogOverlay() {
    if (_dialogObjIdx < 0) {
      _dialogObjIdx = _getKitAPI().object.add({
        name: '__modkit_dialog_overlay',
        events: {
          Create: function (self, other) { self.depth = instance_exists(49) ? -170000 : -1000; },
          Draw: function (self, other) {
            try { dialogDraw(); }
            catch (err) { loud('dialog draw failed (dialog closed)', err); _dialogAbort(); }
          }
        }
      }).index;
    }
    return _dialogObjIdx;
  }

  // split text into pages that each wrap to at most maxLines lines of maxW pixels
  function _paginateText(text, maxLines, maxW) {
    var words = String(text).split(' '), pages = [], cur = '';
    for (var i = 0; i < words.length; i++) {
      var tryText = cur ? cur + ' ' + words[i] : words[i];
      if (cur && _textWrapLines(tryText, maxW) > maxLines) { pages.push(cur); cur = words[i]; }
      else cur = tryText;
    }
    pages.push(cur);
    return pages;
  }

  function _dialogNormalize(lines, opts) {
    var out = [], defaultSpeaker = opts && opts.speaker;
    for (var i = 0; i < lines.length; i++) {
      var l = lines[i];
      if (typeof l === 'string') out.push({ speaker: defaultSpeaker || null, text: l });
      else if (l && typeof l.text === 'string')
        out.push({ speaker: l.speaker || defaultSpeaker || null, text: l.text });
      else throw new Error('showDialog: lines[' + i + '] must be a string or {speaker, text}');
    }
    if (!out.length) throw new Error('showDialog: at least one line required');
    // long lines become several pages of at most three wrapped lines (the box is 3 lines tall)
    var pages = [];
    for (var pi = 0; pi < out.length; pi++) {
      var parts = _paginateText(out[pi].text, 3, 230);
      for (var pj = 0; pj < parts.length; pj++) pages.push({ speaker: out[pi].speaker, text: parts[pj] });
    }
    out = pages;
    return out;
  }

  function _patchLiveEvents(objIndex, entry, key) {
    // The engine copies event functions from the gamedata entry onto the class
    // template at boot, and again onto each instance's carrier object
    // (record._sL) at Create. A wrap installed BEFORE boot is picked up
    // naturally; a wrap installed later (console mid-game) must ALSO patch
    // every live carrier + the template so existing and future instances run it.
    // before the engine has built its class table (mod init runs pre-boot) there is nothing live to patch
    if (typeof _ek === 'undefined' || !_ek) return;
    try {
      var fn = entry[key];
      var n = (typeof instance_number === 'function') ? instance_number(objIndex) : 0;
      for (var i = 0; i < n; i++) {
        var rec = resolveInstance(null, null, instance_find(objIndex, i));
        var holder = rec && rec._sL;
        if (holder && typeof holder === 'object' && typeof fn === 'function')
          holder[key] = fn;
      }
      try {
        if (typeof _ek !== 'undefined' && _ek && typeof _ek._um === 'function') {
          var tpl = _ek._um(objIndex);
          if (tpl && typeof tpl === 'object' && !tpl._sL && typeof fn === 'function')
            tpl[key] = fn;
        }
      } catch (e2) { }
    } catch (e) { log('[modkit] live-event patch skipped for ' + entry[GD.name] + '.' + key + ': ' + (e && e.message)); }
  }

  function _dialogPlayer() {

    var p = resolveInstance(null, null, instance_find(36, 0));
    return (p && typeof p.x === 'number') ? p : null;
  }
  function _dialogFreezePlayer() {
    var p = _dialogPlayer();
    if (p) { p.active = 0; p.image_speed = 0; }
    return p;
  }
  // Error path for the dialog overlay/tick: always drop the key handler,
  // destroy the overlay and give the player back (never leave them frozen).
  function _dialogAbort() {
    var d = dialog;
    try { if (_npcKeyHandler) { window.removeEventListener('keydown', _npcKeyHandler); _npcKeyHandler = null; } } catch (e) {}
    dialog = null;
    if (!d) return;
    try { if (d.ovInst !== undefined && instance_exists(d.ovInst)) instance_destroy(resolveInstance(-4, -4, d.ovInst)); } catch (e) {}
    try { if (d.stock) { if (instance_exists(d.inst)) instance_destroy(resolveInstance(-4, -4, d.inst)); } else _dialogRestorePlayer(d.player); } catch (e) {}
  }
  function _dialogRestorePlayer(p) {
    if (p && instance_exists(p)) { p.active = 1; p.image_speed = 1; }
  }

  function _dialogOpen(lines, opts) {
    opts = opts || {};
    var norm = _dialogNormalize(lines, opts);
    var hasSpeaker = norm.some(function (l) { return !!l.speaker; });
    var stock = (opts.stock !== undefined) ? !!opts.stock
      : !(hasSpeaker || opts.typing);
    if (stock) {

      var tbIdx = findIdx(gamedata[GD.objects], 'obj_TextBox');
      if (tbIdx < 0) throw new Error('showDialog: obj_TextBox not found');
      var texts = norm.map(function (l) { return l.text; });
      var inst = gml_Script_instance_create(-4, -4, 10000, 10000, tbIdx);
      resolveInstance(-4, -4, inst).text = texts;

      dialog = { stock: true, inst: inst, onDone: opts.onDone || null };
      return { stock: true, instance: inst };
    }

    dialog = {
      stock: false,
      suppressZ: !!opts.suppressZ,
      lines: norm,
      idx: 0,
      chars: 0,
      speed: (opts.speed === undefined) ? 30 : opts.speed,
      acc: 0,
      typing: !!opts.typing,
      player: _dialogFreezePlayer(),
      onDone: opts.onDone || null,
      ovInst: gml_Script_instance_create(-4, -4, 0, 0,
        (_dialogObjIdx >= 0) ? _dialogObjIdx : _registerDialogOverlay())
    };
    audio_play_sound(17, 1, 0);
    return { stock: false };
  }

  function _dialogClose(restore) {
    var d = dialog;
    dialog = null;
    if (!d) return;
    if (d.ovInst !== undefined && instance_exists(d.ovInst))
      instance_destroy(resolveInstance(-4, -4, d.ovInst));
    if (d.stock) {
      if (instance_exists(d.inst)) instance_destroy(resolveInstance(-4, -4, d.inst));
    } else if (restore !== false) {
      _dialogRestorePlayer(d.player);
      audio_play_sound(18, 1, 0);
      _inputGrace = 2;
    }
    if (d.onDone) { try { d.onDone(); } catch (e) { loud('dialog onDone failed', e); } }
  }

  function dialogTick(self) {
    if (!dialog) return;
    var d = dialog;
    if (d.stock) {
      if (!instance_exists(d.inst)) { d.player = null; _dialogClose(false); }
      return;
    }
    if (d.mode === 'ask' || d.mode === 'text') return;
    if (d.suppressZ) {
      d.suppressZ = false;
      if (self.pressed_Z) { self.pressed_Z = 0; return; }
    }
    if (d.typing && d.chars < d.lines[d.idx].text.length) {
      d.acc += 1 / gameFPS();
      var nchars = Math.floor(d.acc * d.speed);
      if (nchars > d.chars) d.chars = Math.min(nchars, d.lines[d.idx].text.length);
    }
    if (self.pressed_Z) {
      self.pressed_Z = 0;
      if (d.typing && d.chars < d.lines[d.idx].text.length) {
        d.chars = d.lines[d.idx].text.length;
      } else if (d.idx + 1 < d.lines.length) {
        d.idx++; d.chars = 0; d.acc = 0;
        audio_play_sound(17, 1, 0);
      } else {
        _dialogClose(true);
      }
    }
  }

  // conservative word-wrap line count for the dialog box (font 0, ~7px/char):
  // matches draw_text_ext's wrapping closely enough to size the box so
  // wrapped text never overflows it
  function _textWrapLines(str, maxW) {
    var words = String(str).split(' ');
    var lines = 1, w = 0;
    for (var i = 0; i < words.length; i++) {
      var ww = words[i].length * 7 + 7;
      if (w > 0 && w + ww - 7 > maxW) { lines++; w = ww - 7; }
      else w += ww;
    }
    return lines;
  }

  function dialogDraw() {
    var d = dialog;
    if (!d || d.stock) return;
    var x = gml_Script_xview(-4, -4) + (gml_Script_wview(-4, -4) / 2);
    var y = gml_Script_yview(-4, -4) + gml_Script_hview(-4, -4);
    if (d.mode === 'ask') {
      var nOpt = d.options.length;
      var prLines = _textWrapLines(d.prompt, 230);
      var boxH = 66 + nOpt * 12 + (prLines - 1) * 13;
      draw_set_color(0); draw_set_alpha(0.4);
      draw_rectangle(x - 120, y - 4 - boxH, x + 120, y - 4, 0);
      draw_set_color(8421504); draw_set_alpha(1);
      draw_rect_width(-4, -4, x - 120, y - 4 - boxH, x + 120, y - 4, 1);
      draw_set_halign(0); draw_set_valign(0); draw_set_font(0);
      draw_set_color(12632256);
      draw_text_ext(x - 115, y - boxH + 4, d.prompt, 13, 230);
      for (var ai = 0; ai < nOpt; ai++) {
        var oy = y - boxH + 22 + (prLines - 1) * 13 + ai * 12;
        if (ai === d.sel) { draw_set_color(65535); draw_text(x - 114, oy, '>'); }
        draw_set_color(16777215);
        draw_text_ext(x - 100, oy, d.options[ai].text, 13, 210);
        (function (idx) {
          _mouseZones.push({ x: x - 114, y: oy - 1, w: _zw(d.options[idx].text, 18, 224), h: 12,
            action: function () {
              d.sel = idx;
              if (_npcKeyHandler) _npcKeyHandler({ key: 'Enter', preventDefault: function () {} });
            } });
        })(ai);
      }
      draw_set_color(16777215);
      draw_set_alpha(1);
      return;
    }
    if (d.mode === 'text') {
      // the typed input can wrap to several lines: size the box for the
      // wrapped prompt + wrapped input, keep the bottom edge anchored and
      // push the box top up to fit (single-line case keeps the stock 46px)
      var shown = d.text + (((_gameSteps() % 30) < 15) ? '_' : ' ');
      if (!d.text && d.ph) shown = d.ph;
      var prLinesT = _textWrapLines(d.prompt, 230);
      var inLinesT = _textWrapLines('> ' + shown, 230);
      var promptHT = prLinesT * 13;
      var boxHT = 4 + promptHT + 5 + inLinesT * 13 + 11;
      draw_set_color(0); draw_set_alpha(0.4);
      draw_rectangle(x - 120, y - 4 - boxHT, x + 120, y - 4, 0);
      draw_set_color(8421504); draw_set_alpha(1);
      draw_rect_width(-4, -4, x - 120, y - 4 - boxHT, x + 120, y - 4, 1);
      draw_set_halign(0); draw_set_valign(0); draw_set_font(0);
      draw_set_color(12632256);
      draw_text_ext(x - 115, y - boxHT + 4, d.prompt, 13, 230);
      if (!d.text && d.ph) draw_set_color(8421504);
      else draw_set_color(16777215);
      draw_text_ext(x - 115, y - boxHT + 4 + promptHT + 5, '> ' + shown, 13, 230);
      draw_set_color(12632256);
      draw_text(x + 124, y - 18, 'ENTER');
      _mouseZones.push({ x: x + 122, y: y - 19, w: 42, h: 13,
        action: function () { if (_npcKeyHandler) _npcKeyHandler({ key: 'Enter', preventDefault: function () {} }); } });
      draw_set_color(16777215);
      draw_set_alpha(1);
      return;
    }
    draw_set_color(0);
    draw_set_alpha(0.4);
    draw_rectangle(x - 120, y - 50, x + 120, y - 4, 0);
    draw_set_color(8421504);
    draw_set_alpha(1);
    draw_rect_width(-4, -4, x - 120, y - 50, x + 120, y - 4, 1);
    draw_set_halign(0);
    draw_set_valign(0);
    draw_set_font(0);
    var line = d.lines[d.idx];
    if (line.speaker) {
      draw_set_color(12632256);
      draw_text_ext(x - 115, y - 62, line.speaker, 13, 230);
    }
    draw_set_color(16777215);
    var text = line.text;
    if (d.typing && d.chars < text.length) text = text.slice(0, d.chars);
    draw_text_ext(x - 115, y - 48, text, 13, 230);
    draw_text(x + 124, y - 18, 'Z / J');
    _mouseZones.push({ x: x + 122, y: y - 19, w: 42, h: 13,
      action: function () {
        var cz = null;
        try { cz = resolveInstance(null, null, instance_find(33, 0)); } catch (e) { cz = null; }
        if (cz) cz.pressed_Z = 1;
      } });
    draw_set_color(16777215);
    draw_set_alpha(1);
  }


  function _npcNormalizeText(s) {
    return String(s).replace(/\s+/g, ' ').trim();
  }

  function _npcClose() {
    if (_npcKeyHandler) { window.removeEventListener('keydown', _npcKeyHandler); _npcKeyHandler = null; }
    var d = dialog;
    dialog = null;
    if (!d) return;
    if (d.ovInst !== undefined && instance_exists(d.ovInst))
      instance_destroy(resolveInstance(-4, -4, d.ovInst));
    _dialogRestorePlayer(d.player);
    audio_play_sound(18, 1, 0);
    _inputGrace = 2;
  }

  function _npcStart(mode) {
    if (dialog) throw new Error('npc: a dialog is already open');
    var d = {
      stock: false, mode: mode,
      player: _dialogFreezePlayer(),
      ovInst: gml_Script_instance_create(-4, -4, 0, 0,
        (_dialogObjIdx >= 0) ? _dialogObjIdx : _registerDialogOverlay())
    };
    dialog = d;
    audio_play_sound(17, 1, 0);
    return d;
  }

  function _npcReply(r, raw, idx) {
    if (r === undefined || r === null) return;
    if (typeof r === 'function') { try { r(raw, idx); } catch (e) { loud('npc reply fn failed', e); } return; }
    if (typeof r === 'string') r = [r];
    if (Array.isArray(r)) {
      var lines = [];
      for (var i = 0; i < r.length; i++)
        lines.push(typeof r[i] === 'string' ? { text: r[i] } : r[i]);
      if (lines.length) _dialogOpen(lines, { stock: false, suppressZ: true });
    }
  }

  function npcAsk(spec) {
    if (!spec || typeof spec.prompt !== 'string' || !spec.prompt.trim())
      throw new Error('ask: prompt required');
    if (!Array.isArray(spec.options) || !spec.options.length || spec.options.length > 8)
      throw new Error('ask: options must be a non-empty array (max 8)');
    for (var oi = 0; oi < spec.options.length; oi++) {
      var o = spec.options[oi];
      if (typeof o !== 'string' && !(o && typeof o.text === 'string'))
        throw new Error('ask: options[' + oi + '] must be a string or {text, reply}');
    }
    var d = _npcStart('ask');
    d.prompt = _npcNormalizeText(spec.prompt);
    d.options = spec.options.map(function (op) { return (typeof op === 'string') ? { text: op } : op; });
    d.sel = Math.max(0, Math.min(d.options.length - 1, spec.start || 0));
    var onAnswer = spec.onAnswer || null;
    _npcKeyHandler = function (ev) {
      var k = ev.key;
      if (k === 'ArrowUp' || k === 'w' || k === 'W') {
        ev.preventDefault(); d.sel = (d.sel + d.options.length - 1) % d.options.length;
        audio_play_sound(17, 1, 0);
      } else if (k === 'ArrowDown' || k === 's' || k === 'S') {
        ev.preventDefault(); d.sel = (d.sel + 1) % d.options.length;
        audio_play_sound(17, 1, 0);
      } else if (k === 'Enter' || k === 'z' || k === 'Z' || k === 'j' || k === 'J' || k === ' ') {
        ev.preventDefault();
        var opt = d.options[d.sel], idx = d.sel;
        _npcClose();
        if (onAnswer) { try { onAnswer(idx); } catch (e) { loud('ask onAnswer failed', e); } }
        _npcReply(opt.reply, idx, idx);
        var conA = resolveInstance(-4, -4, instance_find(33, 0));
        if (conA && conA.pressed_Z) conA.pressed_Z = 0;
      } else if (k === 'Escape' || k === 'x' || k === 'X') {
        ev.preventDefault();
        _npcClose();
        if (onAnswer) { try { onAnswer(-1); } catch (e) { loud('ask onAnswer failed', e); } }
      }
    };
    window.addEventListener('keydown', _npcKeyHandler);
    return { options: d.options.length };
  }

  function npcAskText(spec) {
    if (!spec || typeof spec.prompt !== 'string' || !spec.prompt.trim())
      throw new Error('askText: prompt required');
    var cases = Array.isArray(spec.cases) ? spec.cases.slice() : [];
    for (var ci = 0; ci < cases.length; ci++) {
      var c = cases[ci];
      if (typeof c !== 'function' &&
          !(c && (c.match !== undefined || (c.regex && typeof c.regex.test === 'function'))))
        throw new Error('askText: cases[' + ci + '] needs match (string/array/fn) or regex, or be a function');
    }
    var d = _npcStart('text');
    d.prompt = _npcNormalizeText(spec.prompt);
    d.text = '';
    d.maxLen = Math.max(1, Math.min(64, spec.maxLen || 24));
    d.ph = spec.placeholder || '';
    d.cases = cases;
    d.deflt = spec.default || null;
    var caseSensitive = !!spec.caseSensitive;
    var onSubmit = spec.onSubmit || null, onCancel = spec.onCancel || null;
    _npcKeyHandler = function (ev) {
      var k = ev.key;
      if (k === 'Escape') {
        ev.preventDefault();
        var rawC = d.text;
        _npcClose();
        if (onCancel) { try { onCancel(rawC); } catch (e) { loud('askText onCancel failed', e); } }
        else if (onSubmit) { try { onSubmit(rawC, -1); } catch (e) { loud('askText onSubmit failed', e); } }
        return;
      }
      if (k === 'Enter') {
        ev.preventDefault();
        var raw = d.text;
        var norm = caseSensitive ? _npcNormalizeText(raw) : _npcNormalizeText(raw).toLowerCase();
        var hit = -1;
        for (var ci2 = 0; ci2 < d.cases.length && hit < 0; ci2++) {
          var c2 = d.cases[ci2];
          if (typeof c2 === 'function') { if (c2(norm, raw)) hit = ci2; continue; }
          var m = c2.regex ? [c2.regex] :
            (Array.isArray(c2.match) ? c2.match : [c2.match]);
          for (var mi = 0; mi < m.length; mi++) {
            var want = m[mi];
            var ok = false;
            if (want && typeof want.test === 'function') ok = want.test(norm);
            else if (typeof want === 'function') ok = !!want(norm, raw);
            else ok = (caseSensitive ? _npcNormalizeText(want) : _npcNormalizeText(want).toLowerCase()) === norm;
            if (ok) { hit = ci2; break; }
          }
        }
        var act = (hit >= 0) ? d.cases[hit] : d.deflt;
        var rep = null;
        if (typeof act === 'function') rep = act;
        else if (act && act.reply !== undefined) rep = act.reply;
        _npcClose();
        if (onSubmit) { try { onSubmit(raw, hit); } catch (e) { loud('askText onSubmit failed', e); } }
        _npcReply(rep, raw, hit);
        var conB = resolveInstance(-4, -4, instance_find(33, 0));
        if (conB && conB.pressed_Z) conB.pressed_Z = 0;
        return;
      }
      if (k === 'Backspace') { ev.preventDefault(); d.text = d.text.slice(0, -1); return; }
      if (ev.ctrlKey || ev.metaKey || ev.altKey) return;
      if (k.length === 1 && d.text.length < d.maxLen) {
        ev.preventDefault();
        // at most three wrapped lines of input (the box grows two lines past the single-line size)
        if (_textWrapLines('> ' + d.text + k + '_', 230) <= 3) d.text += k;
      }
    };
    window.addEventListener('keydown', _npcKeyHandler);
    return { maxLen: d.maxLen };
  }

  var _npcKeyHandler = null;
  var _inputGrace = 0;
  var _DIALOG_KEYS = ['pressed_up', 'pressed_down', 'pressed_left', 'pressed_right', 'pressed_Z', 'pressed_X',
    'pressed_C', 'pressed_space', 'pressed_P', 'pressed_Q', 'pressed_R', 'pressed_M'];

  var clueRegistry = {};
  var _clueObjIdx = -1, _clueViewIdx = -1;
  var _clueHookedRooms = {};
  var _clueNextSaveIdx = 9;
  var _clueHudWrapped = false;

  function _clueFlag(saveIdx) {
    ini_open('save.ini');
    var v = ini_read_real('save1', 'clue' + saveIdx, 0);
    ini_close();
    return v > 0.5;
  }
  function _clueSetFlag(saveIdx, on) {
    ini_open('save.ini');
    ini_write_real('save1', 'clue' + saveIdx, on ? 1 : 0);
    ini_close();
  }
  function _hudRefresh(hud) {
    hud.clueNumber = ds_list_size(hud.clueList);
    if (hud.clueIndex >= hud.clueNumber) hud.clueIndex = 0;
    hud.currentClue = ds_list_find_value(hud.clueList, hud.clueIndex);
    var w = sprite_get_width(hud.currentClue), h = sprite_get_height(hud.currentClue);
    if (typeof w === 'number' && typeof h === 'number') {
      hud.clueWidth2 = w / 2;
      hud.clueHeight2 = h / 2;
    }
  }
  function _hudHas(hud, sprIdx) {
    var sz = ds_list_size(hud.clueList);
    for (var i = 0; i < sz; i++)
      if (ds_list_find_value(hud.clueList, i) === sprIdx) return true;
    return false;
  }
  function _hudAppendModClues(self, other) {
    var added = 0;
    for (var n in clueRegistry)
      if (Object.prototype.hasOwnProperty.call(clueRegistry, n)) {
        var s = clueRegistry[n];
        if (s.saveIdx === undefined) continue;
        if (!s.found && _clueFlag(s.saveIdx)) s.found = true;
        if (s.found && !_hudHas(self, s.image)) { ds_list_add(self.clueList, s.image); added++; }
      }
    if (added) _hudRefresh(self);
  }
  function _registerClueObjects() {
    if (_clueObjIdx >= 0 && _clueViewIdx >= 0) return;
    var kitApi = _getKitAPI();
    _clueObjIdx = kitApi.object.add({
      name: '__modkit_clue',
      events: {
        Create: function (self, other) { self._modkitClue = null; self.depth = -30; },
        Step: function (self, other) {
          var spec = self._modkitClue;
          if (!spec || (spec.found && spec.once)) return;
          if (dialog) return;
          var player = resolveInstance(self, other, instance_find(36, 0));
          if (typeof player.x !== 'number' || typeof player.y !== 'number') return;
          self._near = Math.abs(player.x - self.x) < spec.radius &&
                      Math.abs(player.y - self.y) < (spec.radius + 8);
          if (!self._near) return;
          _collectClue(self, other, spec);
        },
        Draw: function (self, other) {
          var spec = self._modkitClue;
          if (!spec || (spec.found && spec.once)) return;
          var t = (Date.now() % 1600) / 1600;
          var bob = 0.08 * Math.sin(t * Math.PI * 2);
          var alpha = self._near ? (0.75 + 0.2 * Math.sin(t * Math.PI * 2)) : 0.5 + 0.15 * Math.sin(t * Math.PI * 2);
          try {
            _getKitAPI().sprite.draw(spec.image, self.x, self.y + (self._near ? -1 : 0) * 2,
              { xscale: 1 + bob, yscale: 1 + bob, alpha: alpha });
            if (self._near && spec.hint) {
              draw_set_color(16777215); draw_set_alpha(1); draw_set_halign(1);
              draw_text_ext(self.x, self.y - 24, spec.hint, 13, 200); draw_set_halign(0);
            }
          } catch (err) { loud('clue draw failed', err); }
        }
      }
    }).index;
    _clueViewIdx = kitApi.object.add({
      name: '__modkit_clue_view',
      events: {
        Create: function (self, other) {
          self.depth = -2000;
          self._t = 0;
          self._player = _dialogFreezePlayer();
        },
        Step: function (self, other) {
          self._t++;
          if (self._t < 12) return;
          var con = resolveInstance(self, other, instance_find(33, 0));
          if (con && con.pressed_Z) {
            con.pressed_Z = 0;
            _dialogRestorePlayer(self._player);
            instance_destroy(self);
            audio_play_sound(18, 1, 0);
          }
        },
        Draw: function (self, other) {
          var spec = self._modkitClue;
          if (!spec) return;
          var vx = gml_Script_xview(-4, -4), vy = gml_Script_yview(-4, -4);
          var vw = gml_Script_wview(-4, -4), vh = gml_Script_hview(-4, -4);
          draw_set_color(0); draw_set_alpha(0.55);
          draw_rectangle(vx, vy, vx + vw, vy + vh, 0);
          draw_set_alpha(1);
          var spr = gamedata[GD.sprites][spec.image];
          var w = (spr && spr.width) || 16, h = (spr && spr.height) || 16;
          var scale = Math.min((vw * 0.6) / w, (vh * 0.6) / h);
          try { _getKitAPI().sprite.draw(spec.image, vx + vw / 2, vy + vh / 2 - 10,
                 { xscale: scale, yscale: scale, alpha: 1 }); } catch (err) {}
          draw_set_halign(1); draw_set_valign(0); draw_set_font(0);
          draw_set_color(16777215);
          draw_text_ext(vx + vw / 2, vy + vh / 2 + (h * scale) / 2 + 8, spec.caption || spec.name, 13, vw - 80);
          draw_set_color(12632256);
          draw_text(vx + vw - 34, vy + vh - 18, 'Z / J');
          draw_set_halign(0);
        }
      }
    }).index;
    if (!_clueHudWrapped) {
      _clueHudWrapped = true;
      kitApi.object.wrapEvent('obj_HUD', 'Create', function (orig, self, other) {
        if (orig) orig(self, other);
        try { _hudAppendModClues(self, other); }
        catch (err) { loud('HUD clue append failed', err); }
      });
    }
  }

  // ---- custom inventory items (ModKit.inventory.*) ----
  // Stock slots 0..3 stay as they are; every registered item adds one more slot after them. The bar keeps its
  // 4-slot width and scrolls horizontally (see the obj_Inventory wrappers in 50_menu.js). Ownership is the
  // same save flag save.giveItem / save.hasItem use, keyed by the item id.
  var _invItems = [];
  function _invOwned(id) {
    try {
      var s = _sessionGet(id);
      if (s !== undefined && Number(s) > 0) return true;
      return Number(_saveIniRead(id, 0)) > 0.5;
    } catch (e) { return false; }   // mod init runs before the save layer exists
  }
  function _inventoryAdd(opts) {
    if (!opts || typeof opts !== 'object') throw new Error('inventory.add: options object required');
    var id = String(opts.id == null ? '' : opts.id).replace(/^\$/, '');
    if (!id.length) throw new Error('inventory.add: id required');
    if (_KNOWN_ITEMS.indexOf(id) >= 0) throw new Error('inventory.add: "' + id + '" is a stock item');
    if (opts.image === undefined) throw new Error('inventory.add: image required (sprite index/name, {data} bytes or {url})');
    if (opts.onUse !== undefined && typeof opts.onUse !== 'function') throw new Error('inventory.add: onUse must be a function');
    var rec = { id: id, name: String(opts.name == null ? id : opts.name),
                image: _clueImage({ image: opts.image }, 'inv_' + id),
                frame: (typeof opts.frame === 'number') ? opts.frame : 0, onUse: opts.onUse || null,
                description: opts.description == null ? '' : String(opts.description) };
    for (var i = 0; i < _invItems.length; i++) if (_invItems[i].id === id) { _invItems[i] = rec; return _invInfo(rec, i); }
    _invItems.push(rec);
    return _invInfo(rec, _invItems.length - 1);
  }
  function _invInfo(r, i) { return { id: r.id, name: r.name, description: r.description, image: r.image, slot: 4 + i, owned: _invOwned(r.id) }; }
  // ---- pickup toast: "Got <item>" for save.giveItem on a stock or inventory.add item, mid-game only ----
  var _toasts = [];
  // every event key any object in the game uses ("ev_Create", "ev_Other4_RoomStart", ...): a wrapEvent / patch with
  // a name outside this set (e.g. the source-file spelling "Other_4" instead of "Other4_RoomStart") would never fire
  var _knownEventKeys = null;
  function _checkEventName(who, objName, objEntry, eventName) {
    if (_knownEventKeys === null) {
      _knownEventKeys = {};
      var all = gamedata[GD.objects];
      for (var oi = 0; oi < all.length; oi++)
        if (all[oi]) for (var k in all[oi]) if (k.slice(0, 3) === 'ev_') _knownEventKeys[k] = true;
    }
    var key = 'ev_' + eventName;
    if (objEntry[key] || _knownEventKeys[key]) return;
    var near = [], m = /^([A-Za-z]+)_?(\d+)$/.exec(eventName);
    for (var kk in _knownEventKeys) {
      var nm = kk.slice(3);
      if (m && nm.indexOf(m[1] + m[2] + '_') === 0) near.push(nm);
      else if (nm.toLowerCase() === String(eventName).toLowerCase()) near.push(nm);
    }
    loud(who + ': "' + eventName + '" is not an event name used anywhere in the game, so ' + objName + '.' + eventName +
      ' will never fire' + (near.length ? ' (did you mean ' + near.slice(0, 3).join(' / ') + '?)' : ''));
  }
  var _eventWrappers = [];   // wrapEvent records, so unwrapEvent can switch a wrapper off
  // every field wrapEvent / patchObject changed on an EXISTING object entry, so a mod that fails during init can be
  // rolled back completely (the object table is only truncated for objects the mod added)
  var _objJournal = [];
  function _journalSet(entry, key, value) {
    _objJournal.push({ entry: entry, key: key, had: Object.prototype.hasOwnProperty.call(entry, key), prev: entry[key] });
    entry[key] = value;
  }
  var _STOCK_NAMES = { rope: 'Rope', plunger: 'Plunger', shovel: 'Shovel', lighter: 'Lighter' };
  function _itemToast(id) {
    var nm = _STOCK_NAMES[id];
    for (var i = 0; i < _invItems.length && !nm; i++) if (_invItems[i].id === id) nm = _invItems[i].name;
    if (nm) _toasts.push({ text: 'Got ' + nm, start: -1 });
  }
  // drawn from the HUD (50_menu.js): one toast at a time, ~2.5 s, fading out
  function _toastDraw() {
    if (!_toasts.length) return;
    var t = _toasts[0];
    if (t.start < 0) t.start = _gameStepCount;
    var age = _gameStepCount - t.start, life = 150;
    if (age >= life) { _toasts.shift(); return; }
    var a = age > life - 30 ? (life - age) / 30 : 1;
    var cx = gml_Script_xview(-4, -4) + gml_Script_wview(-4, -4) / 2, y = gml_Script_yview(-4, -4) + 8;
    var w = String(t.text).length * 5 + 16;
    draw_set_alpha(0.7 * a); draw_set_color(0); draw_rectangle(cx - w / 2, y, cx + w / 2, y + 14, 0);
    draw_set_alpha(a); draw_set_color(8421504); draw_rectangle(cx - w / 2, y, cx + w / 2, y + 14, 1);
    draw_set_color(16777215); draw_set_font(0); draw_set_halign(1); draw_set_valign(1);
    draw_text(cx, y + 7, t.text);
    draw_set_halign(0); draw_set_valign(0); draw_set_alpha(1);
  }
  function _inventoryRemove(id) {
    id = String(id == null ? '' : id).replace(/^\$/, '');
    for (var i = 0; i < _invItems.length; i++)
      if (_invItems[i].id === id) { _invItems.splice(i, 1); return true; }
    return false;
  }
  function _inventoryList() {
    var out = [];
    for (var i = 0; i < _invItems.length; i++) out.push(_invInfo(_invItems[i], i));
    return out;
  }

  // removes one clue: destroys its marker, un-finds it in the save, forgets the registration
  function _removeClueNamed(name) {
    if (!clueRegistry.hasOwnProperty(name)) return false;
    var s = clueRegistry[name];
    if (s.inst !== null && instance_exists(s.inst)) instance_destroy(resolveInstance(-4, -4, s.inst));
    if (s.saveIdx !== undefined) { try { _clueSetFlag(s.saveIdx, false); } catch (err) {} }
    delete clueRegistry[name];
    return true;
  }

  function _clueImage(spec, what) {
    var img = spec.image;
    if (typeof img === 'number') return img;
    if (typeof img === 'string' && findIdx(gamedata[GD.sprites], img) >= 0)
      return findIdx(gamedata[GD.sprites], img);
    var opts = { name: '__modkit_clue_img_' + what };
    if (img && img.data !== undefined) opts.data = img.data;
    else if (img && img.url !== undefined) opts.url = img.url;
    else if (typeof img === 'string') opts.url = img;
    else throw new Error('addClue: image must be a sprite index, sprite name, {data} bytes or {url}');
    if (img && img.frameWidth !== undefined) { opts.frames = img.frames; opts.frameWidth = img.frameWidth; opts.frameHeight = img.frameHeight; }
    return _getKitAPI().sprite.addData(opts).index;
  }

  function _collectClue(self, other, spec) {
    spec.found = true;
    log('[modkit] clue found: ' + spec.name);
    audio_play_sound(3, 4, 0);
    var fresh = true;
    if (spec.saveIdx !== undefined) {
      fresh = !_clueFlag(spec.saveIdx);
      _clueSetFlag(spec.saveIdx, true);
    }
    var hud = resolveInstance(self, other, instance_find(25, 0));
    if (hud && typeof hud.x === 'number' && typeof hud.clueNumber === 'number' &&
        (fresh || !_hudHas(hud, spec.image))) {
      ds_list_add(hud.clueList, spec.image);
      _hudRefresh(hud);
    }
    var txtObj = findIdx(gamedata[GD.objects], 'obj_TextTemp');
    if (txtObj >= 0) {
      var txt = gml_Script_instance_create(self, other, -100, -100, txtObj);
      var txtInst = resolveInstance(self, other, txt);
      if (txtInst) txtInst.text = 'Picked up:\nClue!';
    }
    var view = gml_Script_instance_create(self, other, self.x, self.y, _clueViewIdx);
    resolveInstance(self, other, view)._modkitClue = spec;
    if (spec.once) instance_destroy(self);
    if (spec.onFound) {
      try { spec.onFound(spec); }
      catch (err) { loud('clue onFound failed', err); }
    }
  }

  function _cluePlace(spec) {
    if (spec.inst !== null && instance_exists(spec.inst)) return;
    if (spec.saveIdx !== undefined && !spec.found && _clueFlag(spec.saveIdx)) {
      spec.found = true;
      log('[modkit] clue ' + spec.name + ' already found in an earlier session (save flag clue' + spec.saveIdx + ')');
    }
    if (spec.found && spec.once) return;
    var inst = gml_Script_instance_create(-4, -4, spec.x, spec.y, _clueObjIdx);
    resolveInstance(-4, -4, inst)._modkitClue = spec;
    spec.inst = inst;
  }

  function _addClue(opts) {
    if (!opts || !opts.name) throw new Error('addClue: name required');
    if (opts.image === undefined) throw new Error('addClue: image required (sprite index/name, {data} bytes or {url})');
    if (typeof opts.x !== 'number' || typeof opts.y !== 'number') throw new Error('addClue: x and y required (numbers)');
    var roomId = (opts.room === undefined) ? null : opts.room;
    if (roomId === null) {
      try { roomId = _getKitAPI().room.current(); }
      catch (e) { throw new Error('addClue: room required (no live room - mods init before boot)'); }
    } else if (typeof roomId === 'string') roomId = roomIdx(roomId);
    var spec = clueRegistry[opts.name];
    if (spec && (spec.room !== roomId)) {

      if (spec.inst !== null && instance_exists(spec.inst)) instance_destroy(resolveInstance(-4, -4, spec.inst));
      spec.inst = null;
    }
    if (!spec) spec = clueRegistry[opts.name] = { name: opts.name, found: false, inst: null, saveIdx: _clueNextSaveIdx++ };
    spec.room = roomId;
    spec.x = opts.x; spec.y = opts.y;
    spec.radius = (opts.radius === undefined) ? 28 : opts.radius;
    spec.once = (opts.once === undefined) ? true : !!opts.once;
    spec.hint = opts.hint || null;
    spec.caption = opts.caption || null;
    spec.onFound = opts.onFound || null;
    spec.image = _clueImage(opts, opts.name);

    var current = null;
    try { current = _getKitAPI().room.current(); } catch (e) { current = null; }
    if (current === roomId) _cluePlace(spec);
    if (!_clueHookedRooms[roomId]) {
      _clueHookedRooms[roomId] = true;
      _getKitAPI().room.onStart(roomId, function (enteredId) {
        for (var n in clueRegistry)
          if (clueRegistry.hasOwnProperty(n) && clueRegistry[n].room === enteredId)
            _cluePlace(clueRegistry[n]);
      });
    }
    return { name: spec.name, room: roomId, x: spec.x, y: spec.y, found: spec.found };
  }

  function _pngDims(img, what) {
    if (img.length < 24 || String.fromCharCode(img[1], img[2], img[3]) !== 'PNG')
      throw new Error((what || 'image') + ': not a PNG');
    var w = (img[16] << 24 >>> 0) + (img[17] << 16) + (img[18] << 8) + img[19];
    var h = (img[20] << 24 >>> 0) + (img[21] << 16) + (img[22] << 8) + img[23];
    if (!w || !h) throw new Error((what || 'image') + ': empty or corrupt PNG (bad IHDR)');
    return [w, h];
  }

  function _bytesToDataURL(bytes) {
    var s = '';
    for (var i = 0; i < bytes.length; i += 0x8000)
      s += String.fromCharCode.apply(null, bytes.subarray(i, Math.min(i + 0x8000, bytes.length)));
    return 'data:image/png;base64,' + btoa(s);
  }

  function _buildSequence(n, nm, ox, oy) {
    var keys = [];
    for (var keyframeIndex = 0; keyframeIndex < n; keyframeIndex++) keys.push({
      key: keyframeIndex, length: 1, _S1: false, _T1: false,
      _U1: { 0: { imageIndex: keyframeIndex } }
    });
    return {
      name: nm, _F1: 1, _G1: 30, _H1: 0, length: n,
      _I1: ox, _J1: oy, volume: 1, _K1: [],
      _L1: [{
        _M1: 'GMSpriteFramesTrack', name: 'frames',
        _N1: 0, _O1: 0, _P1: 0, _Q1: [], _R1: [], _L1: [],
        _K1: keys
      }],
      _V1: [], _W1: []
    };
  }

  function _overrideSprite(ownerName, spriteName, img, pageRedirect, opts) {
    opts = opts || {};
    var spriteList = gamedata[GD.sprites];
    var sprIdx = -1;
    for (var i = 0; i < spriteList.length; i++)
      if (spriteList[i][GD.name] === spriteName) { sprIdx = i; break; }
    if (sprIdx < 0) throw new Error(ownerName + ': override target not found: ' + spriteName);
    var spr = spriteList[sprIdx];
    var dims = _pngDims(img, spriteName);

    var frameDims = opts.frameDims;
    if (!frameDims) {
      var layout = window.__MODKIT_SPRITE_LAYOUT__;
      if (layout && layout.sprites && layout.sprites[spriteName]) frameDims = layout.sprites[spriteName].dims;
    }
    if (!frameDims) {
      var frameCount = opts.frames || 1;
      if (typeof frameCount !== 'number' || frameCount < 1 || Math.floor(frameCount) !== frameCount)
        throw new Error(spriteName + ': frames must be a whole number >= 1');
      var frameW = opts.frameWidth || Math.floor(dims[0] / frameCount);
      var frameH = opts.frameHeight || dims[1];
      frameDims = [];
      for (var f = 0; f < frameCount; f++) frameDims.push([frameW, frameH]);
    }
    var rowY = opts.rowY || 0;
    var needW = 0, maxH = 0;
    for (var f2 = 0; f2 < frameDims.length; f2++) {
      needW += frameDims[f2][0];
      if (frameDims[f2][1] > maxH) maxH = frameDims[f2][1];
    }
    if (needW > dims[0]) throw new Error(spriteName + ': override png is ' + dims[0] +
      'px wide but its frames need ' + needW + 'px');
    if (rowY + maxH > dims[1]) throw new Error(spriteName + ': override png is ' + dims[1] +
      'px tall but its frames need ' + (rowY + maxH) + 'px (starting at row ' + rowY + ')');

    var pageIdx;
    if (typeof opts.reusePage === 'number') {
      pageIdx = opts.reusePage;
    } else {
      var n = 0, pageName;
      do { pageName = 'mod_' + ownerName + '_ovr' + (n++) + '.png'; }
      while (redirectMap['html5game/' + pageName]);
      pageIdx = gamedata[GD.texturePages].length;
      gamedata[GD.texturePages].push(pageName);
      gamedata[GD.pageMipFlags].push({ MipsToGenerate: 0 });
      redirectMap['html5game/' + pageName] = pageRedirect;
    }
    var rectIds = [], fx = 0;
    for (var f3 = 0; f3 < frameDims.length; f3++) {
      var w = frameDims[f3][0], h = frameDims[f3][1];
      rectIds.push(gamedata[GD.frameRects].push({
        x: fx, y: rowY, _o2: w, _p2: h,
        _08: 2, _18: 0, _28: w, _38: h,
        _48: w, _58: h,
        texturePage: pageIdx
      }) - 1);
      fx += w;
    }
    spr.rectIds = rectIds;
    spr.width = frameDims[0][0];
    spr.height = frameDims[0][1];
    spr._Z1 = 0; spr._A1 = frameDims[0][0]; spr._Y1 = 0; spr._B1 = frameDims[0][1]; spr._C1 = 0;
    var seq = spr.sequence;
    var seqOk = seq && seq._L1 && seq._L1[0] && seq._L1[0]._K1 && seq._L1[0]._K1.length === frameDims.length;
    if (!seqOk) {

      var keys = [];
      for (var k = 0; k < frameDims.length; k++) keys.push({
        key: k, length: 1, _S1: false, _T1: false,
        _U1: { 0: { imageIndex: k } }
      });
      if (seq && seq._L1 && seq._L1[0]) { seq._L1[0]._K1 = keys; seq.length = frameDims.length; }
      else spr.sequence = _buildSequence(frameDims.length, spriteName, 0, 0);
    }

    var group = null;
    for (var gi = 0; gi < gamedata[GD.textureGroups].length; gi++)
      if (gamedata[GD.textureGroups][gi][GD.name] === 'mod_' + ownerName) group = gamedata[GD.textureGroups][gi];
    if (!group) {
      group = { [GD.name]: 'mod_' + ownerName, [GD.groupPages]: [], [GD.groupSprites]: [] };
      gamedata[GD.textureGroups].push(group);
    }
    if (group[GD.groupPages].indexOf(pageIdx) < 0) group[GD.groupPages].push(pageIdx);
    return { index: sprIdx, name: spriteName, rectIds: rectIds, texturePage: pageIdx };
  }

  var forcedRooms = [];
  var roomGotoHooked = false;

  function _exitDirection() {
    var rm = window[EN.roomConstants];
    if (!rm || !rm[EN.currentRoomFn]) return null;
    var roomId;
    try { roomId = rm[EN.currentRoomFn](); } catch (e) { return null; }
    var room = gamedata[GD.rooms][roomId];
    var p = null;
    try {
      if (window.instance_find) {

        p = window.resolveInstance(null, null, window.instance_find(36, 0));
      }
    } catch (e) { p = null; }
    if (!p || !room) return null;

    if (typeof p.x !== 'number' || !isFinite(p.x) ||
        typeof p.y !== 'number' || !isFinite(p.y)) return null;
    var dl = p.x, dr = room.width - p.x, dt = p.y, db = room.height - p.y;
    if (dl <= dr && dl <= dt && dl <= db) return 'west';
    if (dr <= dt && dr <= db) return 'east';
    if (dt <= db) return 'north';
    return 'south';
  }

  function _consumeForcedRoom() {
    if (!forcedRooms.length) return null;
    var dir = _exitDirection();
    for (var i = 0; i < forcedRooms.length; i++) {
      var e = forcedRooms[i];
      if (e.any || (dir !== null && e.dirs.indexOf(dir) >= 0)) {
        forcedRooms.splice(i, 1);
        var name = gamedata[GD.rooms][e.roomId] ? gamedata[GD.rooms][e.roomId][GD.name] : '?';
        log('[modkit] forced room transition -> ' + e.roomId + ' (' + name + '), direction ' +
            (e.any ? 'any' : dir) + ', armed by ' + e.owner);
        return e.roomId;
      }
    }
    return null;
  }

  function _installRoomGotoHook() {
    if (roomGotoHooked) return;
    var orig = window.room_goto;
    if (typeof orig !== 'function') { log('[modkit] forceNextRoom: engine room_goto not found'); return; }
    window.room_goto = function (target) {
      var forced = _consumeForcedRoom();
      if (forced !== null) return orig.call(window, forced);
      return orig.call(window, target);
    };
    roomGotoHooked = true;
  }

  function _pushSprite(ownerName, opts, img) {

        var _dims = _pngDims(img, opts.name), pngWidth = _dims[0], pngHeight = _dims[1];

        if (!pngWidth || !pngHeight) throw new Error(opts.name + ': empty or corrupt PNG (bad IHDR)');
        var frameCount = opts.frames || 1;
        if (typeof frameCount !== 'number' || frameCount < 1 || Math.floor(frameCount) !== frameCount)
          throw new Error(opts.name + ': frames must be a whole number >= 1 (got ' + opts.frames + ')');
        var frameW = opts.frameWidth || Math.floor(pngWidth / frameCount);
        var frameH = opts.frameHeight || pngHeight;
        if (frameW < 1 || frameH < 1) throw new Error(opts.name + ': frameWidth/frameHeight must be >= 1');
        if (frameW * frameCount > pngWidth) throw new Error(opts.name + ': strip too small for ' + frameCount + ' frames');
        if (frameH > pngHeight) throw new Error(opts.name + ': frameHeight (' + frameH + ') taller than the png (' + pngHeight + ')');

        var pageName = opts.pageName || ('mod_' + ownerName + '_tex0.png');
        if (!opts.pageName && redirectMap['html5game/' + pageName]) pageName = _animPageName(ownerName);
        if (redirectMap['html5game/' + pageName]) {
          throw new Error(ownerName + ': texture page ' + pageName + ' is already registered');
        }
        var pageIdx = gamedata[GD.texturePages].length;
        gamedata[GD.texturePages].push(pageName);
        gamedata[GD.pageMipFlags].push({ MipsToGenerate: 0 });
        var pageRedirect = (typeof opts.url === 'string' && opts.url.length)
          ? { url: opts.url }
          : { mod: ownerName, path: opts.file };
        redirectMap['html5game/' + pageName] = pageRedirect;

        var imageId = pageIdx;

        var rectIds = [];
        for (var frameIndex = 0; frameIndex < frameCount; frameIndex++) {
          var frameX = frameIndex * frameW;
          rectIds.push(gamedata[GD.frameRects].push({
            x: frameX, y: 0, _o2: frameW, _p2: frameH,
            _08: 2, _18: 0, _28: frameW, _38: frameH,
            _48: frameW, _58: frameH,
            texturePage: imageId
          }) - 1);
        }

        var origin = opts.origin || [0, 0];
        var spriteIdx = gamedata[GD.sprites].push({
          [GD.name]: opts.name, width: frameW, height: frameH,
          _v1: false, _w1: false, preload: opts.preload !== false,
          _x1: false, originX: origin[0], originY: origin[1],
          _Z1: 0, _A1: frameW, _Y1: 0, _B1: frameH, _C1: 0,
          rectIds: rectIds, _X1: 2,

          sequence: _buildSequence(frameCount, opts.name, origin[0], origin[1])
        }) - 1;

        var group = null;
        for (var groupIndex = 0; groupIndex < gamedata[GD.textureGroups].length; groupIndex++)
          if (gamedata[GD.textureGroups][groupIndex][GD.name] === 'mod_' + ownerName)
            group = gamedata[GD.textureGroups][groupIndex];
        if (!group) {
          group = { [GD.name]: 'mod_' + ownerName, [GD.groupPages]: [], [GD.groupSprites]: [] };
          gamedata[GD.textureGroups].push(group);
        }
        if (group[GD.groupPages].indexOf(pageIdx) < 0) group[GD.groupPages].push(pageIdx);
        group[GD.groupSprites].push(spriteIdx);

        return { index: spriteIdx, name: opts.name, rectIds: rectIds, texturePage: pageIdx };
  }

  var _animPageSeq = 0;
  function _animPageName(ownerName) {
    var name;
    do { name = 'mod_' + ownerName + '_tex' + (_animPageSeq++) + '.png'; }
    while (redirectMap['html5game/' + name]);
    return name;
  }

  function _addSpriteBytes(ownerName, opts, anim) {
    opts.pageName = _animPageName(ownerName);
    opts.url = _bytesToDataURL(anim.strip);
    var r = _pushSprite(ownerName, opts, opts.data);
    gamedata[GD.sprites][r.index]._modkitDelays = anim.delays;
    log('[modkit] ' + ownerName + ': animated sprite ' + opts.name +
      ' (' + anim.format + ', ' + anim.frames + ' frames, ' + anim.frameWidth + 'x' + anim.frameHeight + ')');
    return r;
  }

  // FNV-1a over the WHOLE string (two data: URIs with the same length and
  // header must not share a sound-cache slot).
  function _hashStr(str) {
    var h1 = 0x811c9dc5, h2 = 0x01000193 ^ str.length;
    for (var i = 0; i < str.length; i++) {
      var c = str.charCodeAt(i);
      h1 = Math.imul(h1 ^ c, 16777619) >>> 0;
      h2 = Math.imul(h2 + c, 2246822519) >>> 0;
    }
    return str.length + ':' + h1.toString(16) + h2.toString(16);
  }
  // Readable room vocabulary (same as data/rooms/*.jsonc, see tools/roomfmt.py):
  // tile layers as rows of text ('.' = 0, '#' = 1, or space separated numbers) and
  // sprite elements with named fields. Used by room.addFromJSON.
  function _gridRowsToCells(rows, layerName, lay) {
    var cells = [], w = null;
    for (var y = 0; y < rows.length; y++) {
      if (typeof rows[y] !== 'string') throw new Error('room.addFromJSON: tiles[' + layerName + '] rows must all be strings');
      var row = [];
      if (/^[.#]+$/.test(rows[y])) {
        for (var c = 0; c < rows[y].length; c++) row.push(rows[y].charAt(c) === '#' ? 1 : 0);
      } else {
        var toks = rows[y].trim().split(/\s+/);
        for (var t = 0; t < toks.length; t++) {
          if (toks[t] === '.') row.push(0);
          else if (/^\d+$/.test(toks[t])) row.push(parseInt(toks[t], 10));
          else throw new Error('room.addFromJSON: tiles[' + layerName + '] row ' + y + ' has a bad cell "' + toks[t] + '" (use ., #, or whole numbers)');
        }
      }
      if (w === null) w = row.length;
      else if (row.length !== w) throw new Error('room.addFromJSON: tiles[' + layerName + '] row ' + y + ' has ' + row.length + ' cells, row 0 has ' + w);
      for (var q = 0; q < row.length; q++) cells.push(row[q]);
    }
    return cells;
  }
  // flip: "h" | "v" | "hv" | "none" -> negative scale (how the engine stores a flip)
  function _scaleWithFlip(scale, flip, where) {
    var sx = scale ? scale[0] : 1, sy = scale ? scale[1] : 1;
    var f = (flip === undefined || flip === null || flip === 'none') ? '' : String(flip);
    if (f && !/^(h|v|hv|vh)$/.test(f)) throw new Error(where + ': flip must be none, h, v or hv (got ' + flip + ')');
    if (f.indexOf('h') >= 0) sx = -Math.abs(sx);
    if (f.indexOf('v') >= 0) sy = -Math.abs(sy);
    return [sx, sy];
  }
  // variation sets: room-level "variations": { <type>: { kind: frames|files|sheet, ... } }; an element
  // names one with { type, variant }. Same rules as tools/roomfmt.py resolve_variant.
  //   frames { names: {variant: frame} }  files { variants: {variant: spriteName} } (sprite elements only)
  //   sheet  { sprite?, columns, cells: {variant: [col,row]} }
  function _resolveVariant(el, vsets, where, isInstance) {
    var v = el.variant, t = el.type;
    if (v === undefined || v === null) {
      if (t !== undefined && t !== null) throw new Error(where + ': "type" needs a "variant"');
      return { sprite: null, frame: null };
    }
    if (t === undefined || t === null) {
      if (typeof v === 'number') return { sprite: null, frame: v };
      throw new Error(where + ': variant "' + v + '" needs a "type" naming a set in "variations"');
    }
    var st = vsets && vsets[t];
    if (!st) throw new Error(where + ': no variation set "' + t + '" (defined: ' + (vsets ? Object.keys(vsets).join(', ') : '') + ')');
    function pick(table) {
      if (!table || !Object.prototype.hasOwnProperty.call(table, v))
        throw new Error(where + ': set "' + t + '" has no variant "' + v + '" (has: ' + Object.keys(table || {}).join(', ') + ')');
      return table[v];
    }
    if (st.kind === 'frames') return { sprite: null, frame: pick(st.names) };
    if (st.kind === 'files') {
      if (isInstance) throw new Error(where + ': "files" variation sets swap sprites and only apply to sprite elements; use "frames" for instances');
      return { sprite: pick(st.variants), frame: null };
    }
    if (st.kind === 'sheet') {
      var cell = pick(st.cells);
      if (typeof st.columns !== 'number' || st.columns < 1) throw new Error(where + ': sheet set "' + t + '" needs "columns"');
      if (cell[0] >= st.columns) throw new Error(where + ': cell [' + cell + '] is outside the ' + st.columns + ' columns');
      return { sprite: isInstance ? null : (st.sprite || null), frame: cell[1] * st.columns + cell[0] };
    }
    throw new Error(where + ': variation set "' + t + '" has kind "' + st.kind + '" (use frames, files or sheet)');
  }
  function _friendlyInstance(spec, vsets, where) {
    if (spec.flip === undefined && spec.variant === undefined && spec.type === undefined) return spec;
    var out = clonePlain(spec), sc = _scaleWithFlip(spec.scale, spec.flip, where);
    if (spec.scaleX === undefined) out.scaleX = sc[0];
    if (spec.scaleY === undefined) out.scaleY = sc[1];
    delete out.scale;
    var rv = _resolveVariant(spec, vsets, where, true);
    if (rv.frame !== null) out.imageIndex = rv.frame;
    return out;
  }
  var _spriteElemSeq = 0;
  function _friendlySprite(el, layerName, k, vsets) {
    if (el.sprite === undefined) return clonePlain(el);   // raw element passes through
    var where = 'room.addFromJSON: sprites[' + layerName + '][' + k + ']';
    var rv = _resolveVariant(el, vsets, where, false);
    var sref = rv.sprite !== null ? rv.sprite : el.sprite;
    var si = (typeof sref === 'number') ? sref : findIdx(gamedata[GD.sprites], sref);
    if (si < 0) throw new Error(where + ' no sprite named ' + sref);
    var sc = _scaleWithFlip(el.scale, el.flip, where);
    return { _07: el.name || ('graphic_mod' + (_spriteElemSeq++)), _17: si, _27: el.x, _37: el.y,
             _47: sc[0], _57: sc[1], _67: (el.color === undefined) ? -1 : el.color,
             _77: (el.speed === undefined) ? 1 : el.speed, _87: rv.frame !== null ? rv.frame : (el.frame || 0), _97: el.rotation || 0 };
  }

  function _makeAPI(manifest, files) {

    function readFile(path) {
      var fileBytes = files[path];
      if (!fileBytes) throw new Error(manifest.name + ': missing file ' + path);
      return fileBytes;
    }

    function applyModOverrides() {
      var layout = window.__MODKIT_SPRITE_LAYOUT__ || null;
      var applied = [];
      for (var p in files) {
        var path = String(p);
        if (path.indexOf('overrides/') !== 0) continue;
        if (!/\.(png|gif|apng|bmp)$/i.test(path)) continue;
        var base = path.slice(10).replace(/\.(png|gif|apng|bmp)$/i, '');
        var animF = _decodeAnimated(files[p]);
        if (animF && layout && layout.areas && layout.areas[base])
          throw new Error(manifest.name + ': overrides/' + base + ' - area sheets must stay png strips (animation is per-sprite)');
        if (animF) {
          var dimsA = [];
          for (var ad = 0; ad < animF.frames; ad++) dimsA.push([animF.frameWidth, animF.frameHeight]);
          var rA = _overrideSprite(manifest.name, base, animF.strip,
            { url: _bytesToDataURL(animF.strip) }, { frameDims: dimsA });
          var spriteListA = gamedata[GD.sprites];
          for (var sa = 0; sa < spriteListA.length; sa++)
            if (spriteListA[sa][GD.name] === base) {
              spriteListA[sa]._modkitDelays = animF.delays;
              break;
            }
          log('[modkit] ' + manifest.name + ': animated override of ' + base +
            ' (' + animF.format + ', ' + animF.frames + ' frames)');
          applied.push(path);
          continue;
        }
        if (layout && layout.sprites && layout.sprites[base]) {
          _overrideSprite(manifest.name, base, files[p], { mod: manifest.name, path: path },
            { frameDims: layout.sprites[base].dims });
        } else if (layout && layout.areas && layout.areas[base]) {
          var members = layout.areas[base].members, pageIdx;
          for (var m = 0; m < members.length; m++) {
            var r = _overrideSprite(manifest.name, members[m].name, files[p],
              { mod: manifest.name, path: path },
              { frameDims: layout.sprites[members[m].name].dims, rowY: members[m].y, reusePage: pageIdx });
            if (typeof pageIdx !== 'number') pageIdx = r.texturePage;
          }
        } else {
          throw new Error(manifest.name + ': overrides/' + base + '.png matches no stock sprite ' +
            'or area sheet (names must match tools/split_atlas.py output)');
        }
        applied.push(path);
      }
      return applied;
    }
    manifest.__applyOverrides = function () { return applyModOverrides(); };

    function registerSound(opts) {
      var target = null;
      if (typeof opts.url === 'string' && opts.url.length) target = { url: opts.url };
      else if (typeof opts.data === 'string' && opts.data.length) target = { url: opts.data };
      if (!opts.file && !target) throw new Error('addSound: file, url or data required');
      if (opts.file) readFile(opts.file);
      var entry = {
        [GD.name]: opts.name, _m1: 0, [GD.soundExt]: '', [GD.soundFile]: opts.name, _p1: 0,
        volume: (opts.volume === undefined) ? 1 : opts.volume,
        _q1: 0, preload: false, _r1: 0
      };
      gamedata[GD.sounds].push(entry);
      var redirect = target || { mod: manifest.name, path: opts.file };
      redirectMap['html5game/' + opts.name] = redirect;
      redirectMap['html5game/' + opts.name + '.ogg'] = redirect;
      return { index: gamedata[GD.sounds].length - 1, name: opts.name };
    }

    var soundCache = {};
    var autoSoundSeq = 0;

    function mimeFor(path) {
      var dot = path.lastIndexOf('.'), ext = (dot < 0) ? '' : path.slice(dot + 1).toLowerCase();
      if (ext === 'png') return 'image/png';
      if (ext === 'jpg' || ext === 'jpeg') return 'image/jpeg';
      if (ext === 'gif') return 'image/gif';
      if (ext === 'webp') return 'image/webp';
      if (ext === 'svg') return 'image/svg+xml';
      if (ext === 'ogg') return 'audio/ogg';
      if (ext === 'wav') return 'audio/wav';
      if (ext === 'mp3') return 'audio/mpeg';
      if (ext === 'js') return 'text/javascript';
      if (ext === 'json' || ext === 'jsonc') return 'application/json';
      return 'application/octet-stream';
    }

    function fileURL(path) {
      var key = manifest.name + '/' + path;
      if (blobCache[key]) return blobCache[key];
      var fileBytes = readFile(path);
      var mimeType = mimeFor(path);
      var url;
      var ext = (path.indexOf('.') >= 0) ? path.slice(path.lastIndexOf('.') + 1).toLowerCase() : '';
      var isImage = (ext === 'png' || ext === 'jpg' || ext === 'jpeg' || ext === 'gif');

      if (isImage || typeof URL === 'undefined' || !URL.createObjectURL || typeof Blob === 'undefined') {
        var binaryString = '';
        for (var i = 0; i < fileBytes.length; i++) binaryString += String.fromCharCode(fileBytes[i]);
        url = 'data:' + mimeType + ';base64,' + ((typeof btoa === 'function') ? btoa(binaryString) : binaryString);
      } else {

        url = URL.createObjectURL(new Blob([fileBytes], { type: mimeType }));
      }
      if (typeof url !== 'string' || !url.length) throw new Error('fileURL: could not build a URL for ' + path);
      blobCache[key] = url;
      return url;
    }

    // makeAPI deletes the flat names after namespacing, so keep our own handle
    var _addRoomRef;
    var api = {

      info: manifest,
      files: Object.keys(files),
      file: readFile,
      fileURL: fileURL,
      decodeText: decodeText,
      parseJSONC: parseJSONC,

      obj: objIdx, sprite: sprIdx, sound: sndIdx, room: roomIdx,

      spriteName: function (index) { return _nameAt(gamedata[GD.sprites], index, 'sprite'); },
      objectName: function (index) { return _nameAt(gamedata[GD.objects], index, 'object'); },
      soundName: function (index) { return _nameAt(gamedata[GD.sounds], index, 'sound'); },
      roomName: function (index) { return _nameAt(gamedata[GD.rooms], index, 'room'); },

      listSprites: function (query) {
        return _listTable(gamedata[GD.sprites], query, _spriteRow);
      },
      listObjects: function (query) {
        return _listTable(gamedata[GD.objects], query, _objectRow);
      },
      listSounds: function (query) {
        return _listTable(gamedata[GD.sounds], query, _soundRow);
      },
      listRooms: function (query) {
        return _listTable(gamedata[GD.rooms], query, _roomRow);
      },

      find: function (query) {
        var q = String(query === undefined || query === null ? '' : query);
        if (!q) throw new Error('find: pass a name fragment to search for');
        var out = [];
        var tag = function (type, arr) {
          for (var k = 0; k < arr.length; k++) { arr[k].type = type; out.push(arr[k]); }
        };
        tag('sprite', _listTable(gamedata[GD.sprites], q, _spriteRow));
        tag('object', _listTable(gamedata[GD.objects], q, _objectRow));
        tag('sound', _listTable(gamedata[GD.sounds], q, _soundRow));
        tag('room', _listTable(gamedata[GD.rooms], q, _roomRow));
        return out;
      },

      wrapFunction: function (name, wrapper) {
        if (typeof name !== 'string' || !name) throw new Error('wrapFunction: function name required');
        if (typeof wrapper !== 'function') throw new Error('wrapFunction: wrapper must be a function');
        var cur = window[name];
        if (typeof cur !== 'function') throw new Error('wrapFunction: no global function named ' + name);
        var prev = cur;
        var wrapped = function () { return wrapper(prev, this, arguments); };
        window[name] = wrapped;
        fnWraps.push({ name: name, fn: wrapped, prev: prev });
        return { name: name, restore: function () {
          if (window[name] === wrapped) window[name] = prev;
          for (var i = fnWraps.length - 1; i >= 0; i--) if (fnWraps[i].fn === wrapped) { fnWraps.splice(i, 1); break; }
          return true;
        } };
      },

      eventSource: function (objName, eventName) {
        var idx = objIdx(objName);
        var entry = gamedata[GD.objects][idx];
        var key = 'ev_' + eventName;
        var fn = entry[key];
        if (typeof fn !== 'function') throw new Error(objName + ' has no ' + eventName + ' event');
        return { object: objName, index: idx, event: eventName, source: String(fn) };
      },

      bootLog: function () { return bootLog.slice(); },

      currentRoom: function () {
        var rm = window[EN.roomConstants];
        if (!rm || !rm[EN.currentRoomFn]) throw new Error('currentRoom: the game has not started yet');
        var cur = null;
        try { cur = rm[EN.currentRoomFn](); }
        catch (e) { cur = null; }
        if (cur === null || cur === undefined) throw new Error('currentRoom: no room is open yet');
        return cur;
      },

      viewPos: function () {
        // camera/view origin + size - rooms bigger than the 400x300 view
        // (rm_TombInterior 400x800, rm_Dungeon 1920x1440) scroll it with the
        // player, so on-screen overlay text must be drawn at view-relative
        // coords (x + view.x, y + view.y). 400x300 rooms never scroll.
        var vx = 0, vy = 0, vw = 0, vh = 0;
        try {
          vx = gml_Script_xview(-4, -4); vy = gml_Script_yview(-4, -4);
          vw = gml_Script_wview(-4, -4); vh = gml_Script_hview(-4, -4);
        } catch (e) { }
        if (!vw || !vh) {
          // no view initialized yet (pre-boot/headless): fall back to room size
          try { vw = _Mb.room_width || 0; vh = _Mb.room_height || 0; } catch (e2) { }
        }
        return { x: Number(vx) || 0, y: Number(vy) || 0, w: Number(vw) || 0, h: Number(vh) || 0 };
      },

      player: function () {
        var p = null;

        try {
          if (window.instance_find && window.resolveInstance)
            p = window.resolveInstance(null, null, window.instance_find(36, 0));
        } catch (e) { p = null; }
        if (!p || typeof p.x !== 'number' || !isFinite(p.x) ||
            typeof p.y !== 'number' || !isFinite(p.y)) return null;
        var out = { x: p.x, y: p.y, id: p.id };
        try { out.speed = p.speed; out.direction = p.direction; } catch (e) {}
        try { out.room = this.room.current(); } catch (e) { out.room = null; }
        return out;
      },

      mod: function (name) {
        if (!allMods) return null;
        for (var i = 0; i < allMods.length; i++) {
          var m = allMods[i];
          if (m.name !== name) continue;
          var active = false;
          for (var j = 0; j < loadedMods.length; j++) if (loadedMods[j] === m) { active = true; break; }
          if (!active) return null;
          var owner = m;
          return {
            name: owner.name, title: owner.title || owner.name,
            version: owner.version || '', author: owner.author || '',
            description: owner.description || '', manifest: owner,
            dependencies: owner.dependencies || [],
            file: function (path) {
              var b = owner.__files[path];
              if (!b) throw new Error(owner.name + ': missing file ' + path);
              return b;
            }
          };
        }
        return null;
      },

      forceNextRoom: function (room, directions) {
        var roomId = room;
        if (typeof room === 'string') {
          roomId = roomIdx(room);
          if (roomId < 0) throw new Error('forceNextRoom: unknown room name: ' + room);
        } else if (typeof room !== 'number' || Math.floor(room) !== room) {
          throw new Error('forceNextRoom: room must be a room id (number) or a room name (string)');
        }
        if (!(roomId >= 0 && roomId < gamedata[GD.rooms].length))
          throw new Error('forceNextRoom: room id out of range: ' + room);
        if (gamedata[GD.rooms][roomId] && gamedata[GD.rooms][roomId].__modkitRemoved)
          throw new Error('forceNextRoom: room ' + roomId + ' was removed');
        var raw = (directions === undefined || directions === null) ? 'any' : directions;
        if (typeof raw === 'string') raw = raw.split(',');
        if (!Array.isArray(raw))
          throw new Error('forceNextRoom: direction must be "east"/"north"/"west"/"south"/"any", or an array of them');
        var dirs = [];
        for (var d = 0; d < raw.length; d++) {
          var one = String(raw[d]).trim().toLowerCase();
          if (one !== 'east' && one !== 'north' && one !== 'west' && one !== 'south' && one !== 'any')
            throw new Error('forceNextRoom: unknown direction: ' + raw[d] + ' (use east/north/west/south/any)');
          if (dirs.indexOf(one) < 0) dirs.push(one);
        }
        var entry = { roomId: roomId, dirs: dirs, any: dirs.indexOf('any') >= 0, owner: manifest.name };
        forcedRooms.push(entry);
        _installRoomGotoHook();
        return {
          cancel: function () {
            var i = forcedRooms.indexOf(entry);
            if (i >= 0) { forcedRooms.splice(i, 1); return true; }
            return false;
          }
        };
      },
      _forcedRoomsPending: function () { return forcedRooms.length; },

      patchObject: function (name, patch) {
        var objEntry = gamedata[GD.objects][objIdx(name)];
        if (patch.sprite !== undefined) _journalSet(objEntry, GD.objSprite,
          (typeof patch.sprite === 'number') ? patch.sprite : sprIdx(patch.sprite));
        if (patch.parent !== undefined) _journalSet(objEntry, 'parent',
          (typeof patch.parent === 'number') ? patch.parent : objIdx(patch.parent));
        if (patch.visible !== undefined) _journalSet(objEntry, 'visible', !!patch.visible);
        if (patch.events) {
          for (var eventName in patch.events) if (patch.events.hasOwnProperty(eventName)) {
            var eventFn = patch.events[eventName];
            if (typeof eventFn !== 'function') throw new Error(name + '.' + eventName + ': event must be a function');
            _checkEventName('object.patch', name, objEntry, eventName);
            _journalSet(objEntry, 'ev_' + eventName, eventFn);
          }
        }
        return objEntry;
      },

      wrapEvent: function (name, eventName, wrapper) {
        if (typeof wrapper !== 'function') throw new Error('wrapEvent: wrapper must be a function (got ' + typeof wrapper + ')');
        var objIndex = objIdx(name);
        var objEntry = gamedata[GD.objects][objIndex], key = 'ev_' + eventName;
        _checkEventName('object.wrapEvent', name, objEntry, eventName);
        var orig = objEntry[key] || null;
        var wrec = { entry: objEntry, key: key, wrapper: wrapper, disabled: false };
        _eventWrappers.push(wrec);
        _journalSet(objEntry, key, function (self, other) {
          // an unwrapped wrapper stays in the chain (other mods may wrap on top) but passes straight through
          if (wrec.disabled) return orig ? orig(self, other) : undefined;
          try { return wrapper(orig, self, other); }
          catch (err) {

            loud('event ' + name + '.' + eventName + ' failed (rethrown)', err);
            throw err;
          }
        });
        _patchLiveEvents(objIndex, objEntry, key);
        return objEntry;
      },

      unwrapEvent: function (name, eventName, wrapper) {
        var objEntry = gamedata[GD.objects][objIdx(name)], key = 'ev_' + eventName;
        for (var i = _eventWrappers.length - 1; i >= 0; i--) {
          var w = _eventWrappers[i];
          if (w.entry === objEntry && w.key === key && w.wrapper === wrapper && !w.disabled) { w.disabled = true; return true; }
        }
        return false;
      },

      addObject: function (opts) {
        if (!opts || !opts.name) throw new Error('addObject: name required');
        if (findIdx(gamedata[GD.objects], opts.name) >= 0) throw new Error('addObject: ' + opts.name + ' already exists');
        var entry = {
          [GD.name]: opts.name,
          [GD.objSprite]: (opts.sprite === undefined || opts.sprite === null) ? -1 :
               (typeof opts.sprite === 'number' ? opts.sprite : sprIdx(opts.sprite)),
          visible: opts.visible !== false,
          parent: (opts.parent === undefined) ? -100 :
                  (typeof opts.parent === 'number' ? opts.parent : objIdx(opts.parent)),
          ev_PreCreate: function (self, other) {},
          _A2: [], _B2: []
        };
        if (opts.events) for (var eventName in opts.events) if (opts.events.hasOwnProperty(eventName)) {
          if (typeof opts.events[eventName] !== 'function') throw new Error(opts.name + '.' + eventName + ': must be a function');
          entry['ev_' + eventName] = opts.events[eventName];
        }
        gamedata[GD.objects].push(entry);
        return { index: gamedata[GD.objects].length - 1, name: opts.name };
      },

      addSound: function (opts) {
        if (!opts.name || (!opts.file && !opts.url && !opts.data))
          throw new Error('addSound: name and file (or url/data) required');
        return registerSound(opts);
      },

      addClue: function (opts) {
        if (_clueObjIdx < 0) _registerClueObjects();
        return _addClue(opts);
      },

      listClues: function () {
        var out = [];
        for (var n in clueRegistry)
          if (clueRegistry.hasOwnProperty(n)) {
            var s = clueRegistry[n];
            out.push({ name: s.name, room: s.room, x: s.x, y: s.y, found: s.found, once: s.once, image: s.image, saveIdx: s.saveIdx });
          }
        return out;
      },
      clueFound: function (name) { return !!(clueRegistry[name] && clueRegistry[name].found); },
      nearestClue: function (x, y) {
        if (typeof x !== 'number' || typeof y !== 'number') return null;
        var rid;
        try { rid = _getKitAPI().room.current(); } catch (err) { return null; }
        var best = null, bd = Infinity;
        for (var n in clueRegistry)
          if (Object.prototype.hasOwnProperty.call(clueRegistry, n)) {
            var s = clueRegistry[n];
            if (s.found || s.room !== rid) continue;
            var d = (s.x - x) * (s.x - x) + (s.y - y) * (s.y - y);
            if (d < bd) { bd = d; best = s; }
          }
        if (!best) return null;
        return { name: best.name, x: best.x, y: best.y, distance: Math.sqrt(bd), image: best.image };
      },
      removeClue: function (name) { return _removeClueNamed(name); },
      clearClues: function () {
        for (var n in clueRegistry)
          if (clueRegistry.hasOwnProperty(n)) _removeClueNamed(n);
        return true;
      },

      playSound: function (opts) {
        if (!opts || (!opts.file && !opts.url && !opts.data))
          throw new Error('playSound: file, url or data required');
        var source = (typeof opts.url === 'string' && opts.url.length) ? opts.url :
                     ((typeof opts.data === 'string' && opts.data.length) ? opts.data : null);
        var key = opts.file ? ('f:' + opts.file)
          : (source ? ('s:' + _hashStr(String(source))) : null);
        var idx = key !== null ? soundCache[key] : undefined;
        if (idx === undefined) {
          var name = opts.name || (manifest.name + '_snd' + (++autoSoundSeq));
          idx = registerSound({ name: name, file: opts.file, url: opts.url, data: opts.data,
                                volume: opts.volume }).index;
          if (key !== null) soundCache[key] = idx;
        }
        if (typeof audio_play_sound !== 'function')
          throw new Error('playSound: audio system not ready (call from an event, not mod init)');
        audio_play_sound(idx, opts.priority === undefined ? 10 : opts.priority, opts.loop ? 1 : 0);
        return idx;
      },

      addSprite: function (opts) {
        if (!opts.name || !opts.file) throw new Error('addSprite: name and file required');
        var bytes = readFile(opts.file);
        var anim = _decodeAnimated(bytes);
        if (anim) {
          var so = { name: opts.name, data: anim.strip, frames: anim.frames,
                     frameWidth: anim.frameWidth, frameHeight: anim.frameHeight,
                     origin: opts.origin, preload: opts.preload };
          return _addSpriteBytes(manifest.name, so, anim);
        }
        return _pushSprite(manifest.name, opts, bytes);
      },

      overrideSprite: function (def) {

        if (!def || !def.name) throw new Error('overrideSprite: name required');
        var img, redirect, ovOpts = { frames: def.frames, frameWidth: def.frameWidth, frameHeight: def.frameHeight };
        if (def.file) { img = readFile(def.file); redirect = { mod: manifest.name, path: def.file }; }
        else if (def.data && typeof def.data !== 'string') { img = def.data; redirect = { url: _bytesToDataURL(def.data) }; }
        else if (def.data) throw new Error('overrideSprite: data must be image bytes');
        else throw new Error('overrideSprite: file or data required');
        var anim = _decodeAnimated(img);
        if (anim) {
          img = anim.strip;
          redirect = { url: _bytesToDataURL(anim.strip) };
          ovOpts = { frameDims: null, frames: anim.frames, frameWidth: anim.frameWidth, frameHeight: anim.frameHeight };
          var dims = [];
          for (var fdi = 0; fdi < anim.frames; fdi++) dims.push([anim.frameWidth, anim.frameHeight]);
          ovOpts.frameDims = dims;
          log('[modkit] ' + manifest.name + ': animated override of ' + def.name +
            ' (' + anim.format + ', ' + anim.frames + ' frames)');
        }
        var r = _overrideSprite(manifest.name, def.name, img, redirect, ovOpts);
        if (anim) {
          var spriteList = gamedata[GD.sprites];
          for (var oi = 0; oi < spriteList.length; oi++)
            if (spriteList[oi][GD.name] === def.name) {
              spriteList[oi]._modkitDelays = anim.delays;
              break;
            }
        }
        return r;
      },
      addSpriteData: function (opts) {
        if (!opts || !opts.name || !opts.data) throw new Error('addSpriteData: name and data required');
        var _o = {}; for (var _k in opts) if (Object.prototype.hasOwnProperty.call(opts, _k)) _o[_k] = opts[_k];
        opts = _o;
        var anim = _decodeAnimated(opts.data);
        if (anim)
          return _addSpriteBytes(manifest.name, { name: opts.name, data: anim.strip, frames: anim.frames,
            frameWidth: anim.frameWidth, frameHeight: anim.frameHeight, origin: opts.origin,
            preload: opts.preload }, anim);
        if (opts.url === undefined) {

          var bin = '';
          for (var i = 0; i < opts.data.length; i += 8192)
            bin += String.fromCharCode.apply(null, opts.data.subarray ? opts.data.subarray(i, i + 8192) : opts.data.slice(i, i + 8192));
          opts.url = 'data:image/png;base64,' + btoa(bin);
        }
        return _pushSprite(manifest.name, opts, opts.data);
      },

      addSpriteFile: function (opts) {
        if (!opts.name || !opts.page) throw new Error('addSpriteFile: name and page required');
        var w = opts.width, h = opts.height;
        if (!(w > 0) || !(h > 0)) throw new Error('addSpriteFile: width and height required');
        var pageName = String(opts.page).replace(/^.*\//, '');
        if (!/^[\w.-]+\.png$/i.test(pageName)) throw new Error('addSpriteFile: page must be a png file name (got ' + opts.page + ')');
        var pageIdx = gamedata[GD.texturePages].length;
        gamedata[GD.texturePages].push(pageName);
        gamedata[GD.pageMipFlags].push({ MipsToGenerate: 0 });
        var rectIds = [gamedata[GD.frameRects].push({
          x: 0, y: 0, _o2: w, _p2: h,
          _08: 2, _18: 0, _28: w, _38: h,
          _48: w, _58: h, texturePage: pageIdx
        }) - 1];
        var origin = opts.origin || [0, 0];
        var spriteIdx = gamedata[GD.sprites].push({
          [GD.name]: opts.name, width: w, height: h,
          _v1: false, _w1: false, preload: opts.preload !== false,
          _x1: false, originX: origin[0], originY: origin[1],
          _Z1: 0, _A1: w, _Y1: 0, _B1: h, _C1: 0,
          rectIds: rectIds, _X1: 2,
          sequence: _buildSequence(1, opts.name, origin[0], origin[1])
        }) - 1;
        var group = null;
        for (var gi = 0; gi < gamedata[GD.textureGroups].length; gi++)
          if (gamedata[GD.textureGroups][gi][GD.name] === 'mod_' + manifest.name)
            group = gamedata[GD.textureGroups][gi];
        if (!group) {
          group = { [GD.name]: 'mod_' + manifest.name, [GD.groupPages]: [], [GD.groupSprites]: [] };
          gamedata[GD.textureGroups].push(group);
        }
        if (group[GD.groupPages].indexOf(pageIdx) < 0) group[GD.groupPages].push(pageIdx);
        group[GD.groupSprites].push(spriteIdx);
        return { index: spriteIdx, name: opts.name, rectIds: rectIds, texturePage: pageIdx };
      },

      drawImage: function (spriteIndex, x, y, opts) {
        if (typeof draw_sprite_ext !== 'function') throw new Error('drawImage: draw_sprite_ext unavailable');
        var spr = gamedata[GD.sprites][spriteIndex];
        if (!spr) throw new Error('drawImage: unknown sprite index ' + spriteIndex);
        opts = opts || {};
        var w = spr.width || 1, h = spr.height || 1;
        var xs = (opts.xscale !== undefined) ? opts.xscale : (opts.width !== undefined ? opts.width / w : 1);
        var ys = (opts.yscale !== undefined) ? opts.yscale : (opts.height !== undefined ? opts.height / h : 1);
        var frames = spr.rectIds ? spr.rectIds.length : 1;
        var subimg = (opts.subimg !== undefined) ? opts.subimg : opts.frame;
        if (subimg === undefined && frames > 1 && opts.animate !== false) {
          var del = spr._modkitDelays;
          var t = (opts.time !== undefined) ? (Number(opts.time) || 0)
                  : (_gameSteps() * 1000) / 60;
          var totalMs = 0;
          if (del && opts.fps === undefined) for (var di = 0; di < del.length; di++) totalMs += del[di];
          if (del && opts.fps === undefined && totalMs > 0) {
            var tm = ((t % totalMs) + totalMs) % totalMs, acc = 0;
            subimg = frames - 1;
            for (var dj = 0; dj < del.length; dj++) {
              if (acc + del[dj] > tm) { subimg = dj; break; }
              acc += del[dj];
            }
          } else {
            var fps = opts.fps === undefined ? 12 : opts.fps;
            if (!(fps > 0)) fps = 12;
            subimg = Math.floor((t * fps) / 1000) % frames;
          }
        }
        if (subimg === undefined || subimg < 0) subimg = 0;
        if (subimg >= frames) subimg = frames - 1;
        draw_sprite_ext({ image_index: 0 }, spriteIndex, subimg, x, y, xs, ys,
          opts.rot || 0, (opts.color === undefined ? 16777215 : opts.color),
          (opts.alpha === undefined ? 1 : opts.alpha));
        return subimg;
      },
      _gameSteps: function () { return _gameSteps(); },

      setMenuBackground: function (spriteIndex, opts) {
        return setMenuBackgroundInternal(spriteIndex, opts);
      },
      clearMenuBackground: function () {
        menuBackground = null;
      },

      redirectAsset: function (original, target) {
        if (typeof target !== 'string' || !target.length) throw new Error('redirectAsset: target required');
        var key = original.indexOf('/') < 0 ? ('html5game/' + original) : original;
        if (/^https?:\/\//.test(target)) {
          redirectMap[key] = { url: target };
        } else {

          readFile(target);
          redirectMap[key] = { mod: manifest.name, path: target };
        }
      },

      addInstance: function (opts) {
        if (!opts || opts.room === undefined || opts.room === null || opts.room === '' || opts.object === undefined || opts.x === undefined || opts.y === undefined)
          throw new Error('addInstance: room, object, x, y required');
        var roomId = (typeof opts.room === 'number') ? opts.room : roomIdx(opts.room);
        var room = gamedata[GD.rooms][roomId];
        if (room && room.__modkitRemoved) throw new Error('addInstance: room ' + roomId + ' was removed');

        var live = null;
        try {
          var rmL = window[EN.roomConstants];
          if (rmL && rmL[EN.currentRoomFn]) live = rmL[EN.currentRoomFn]();
        } catch (e) { live = null; }
        if (live === roomId)
          throw new Error('addInstance: room ' + roomId + ' is currently loaded; add to it before it loads (mod init) or pick a non-current room');
        var objIndex = (typeof opts.object === 'number') ? opts.object : objIdx(opts.object);
        var id = _nextInstanceId++;
        var inst = {
          x: opts.x, y: opts.y, index: objIndex, id: id,
          rotation: 0, scaleX: 1, scaleY: 1,
          imageSpeed: (opts.imageSpeed === undefined) ? 1 : opts.imageSpeed,
          imageIndex: (opts.imageIndex === undefined) ? 0 : opts.imageIndex,
          [GD.instColor]: 4294967295
        };
        if (opts.creationCode) inst[GD.instCreationCode] = opts.creationCode;
        room[GD.roomInstances].push(inst);

        var layer = null;
        for (var i = 0; i < room[GD.roomLayers].length; i++) {
          var layerEntry = room[GD.roomLayers][i];
          if (layerEntry.type === 2) { layer = layerEntry; if (opts.layer !== undefined && layerEntry[GD.name] === opts.layer) break; }
        }
        if (!layer) for (i = 0; i < room[GD.roomLayers].length; i++) if (room[GD.roomLayers][i].type === 2) { layer = room[GD.roomLayers][i]; break; }
        if (layer) {
          layer[GD.layerInstanceIds] = layer[GD.layerInstanceIds] || [];
          layer[GD.layerInstanceIds].push(id);
          // the record's element count must track the id list or the engine's
          // record -> live conversion skips the extra element (stock layer
          // records always carry _e7 == _f7.length)
          layer[GD.layerInstanceCount] = layer[GD.layerInstanceIds].length;
        }
        return id;
      },

      removeRoom: function (room) {
        var id = _resolveRoomRef(room, 'room.remove');
        if (stockCounts && id < stockCounts.rooms)
          throw new Error('room.remove: room ' + id + ' (' + gamedata[GD.rooms][id][GD.name] + ') is a stock room - only mod-added rooms can be removed');
        if (_liveRoomId() === id) {
          // can't pull the floor out from under the player: retire it as soon
          // as the next room starts (call from room.onEnd for a 1-time room)
          if (_pendingRoomRemovals.indexOf(id) < 0) _pendingRoomRemovals.push(id);
          return { removed: false, deferred: true };
        }
        _retireRoom(id);
        return { removed: true, deferred: false };
      },

      removeRoomInstance: function (room, instanceId) {
        var id = _resolveRoomRef(room, 'room.removeInstance');
        if (typeof instanceId !== 'number') throw new Error('room.removeInstance: instance id (the number room.addInstance returned) required');
        if (_liveRoomId() === id)
          throw new Error('room.removeInstance: room ' + id + ' is currently loaded - use instance_destroy on the live instance instead');
        var rec = gamedata[GD.rooms][id], list = rec[GD.roomInstances] || [], found = false;
        for (var i = list.length - 1; i >= 0; i--) if (list[i].id === instanceId) { list.splice(i, 1); found = true; }
        var layers = rec[GD.roomLayers] || [];
        for (var li = 0; li < layers.length; li++) {
          var ids = layers[li][GD.layerInstanceIds];
          if (!ids) continue;
          var at = ids.indexOf(instanceId);
          if (at >= 0) { ids.splice(at, 1); layers[li][GD.layerInstanceCount] = ids.length; }
        }
        return found;
      },

      patchRoom: function (name, fn) {
        fn(gamedata[GD.rooms][(typeof name === 'number') ? name : roomIdx(name)]);
      },

      addRoomFromJSON: function (data) {
        var spec = (typeof data === 'string') ? parseJSONC(data) : data;
        if (!spec || typeof spec !== 'object' || Array.isArray(spec))
          throw new Error('room.addFromJSON: a JSON/JSONC object required');
        if (!spec.name) throw new Error('room.addFromJSON: "name" required');
        var bg = spec.background;
        if (typeof bg === 'string') {
          var mm = /^rgb\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)$/.exec(bg.trim());
          if (!mm) throw new Error('room.addFromJSON: background must be "rgb(r,g,b)" or a BGR integer');
          bg = (parseInt(mm[1], 10) & 255) | ((parseInt(mm[2], 10) & 255) << 8) | ((parseInt(mm[3], 10) & 255) << 16);
        }
        var addR = (this && typeof this.add === 'function') ? this.add : _addRoomRef;
        var idx = addR({
          name: spec.name, base: spec.base, width: spec.width, height: spec.height,
          background: bg, speed: spec.speed, persistent: spec.persistent,
          clearTiles: spec.clearTiles, clearSprites: spec.clearSprites,
          instances: (spec.instances || []).map(function (ins, n) { return _friendlyInstance(ins, spec.variations, 'room.addFromJSON: instances[' + n + ']'); })
        });
        var record = gamedata[GD.rooms][idx];
        var layers = record[GD.roomLayers] || [];
        function findLayer(name, type) {
          for (var li = 0; li < layers.length; li++)
            if (layers[li][GD.name] === name && layers[li].type === type) return layers[li];
          return null;
        }
        if (spec.tiles !== undefined) {
          if (!spec.tiles || typeof spec.tiles !== 'object' || Array.isArray(spec.tiles))
            throw new Error('room.addFromJSON: "tiles" must be an object of { layerName: [ints] }');
          for (var tname in spec.tiles) {
            if (!Object.prototype.hasOwnProperty.call(spec.tiles, tname)) continue;
            var arr = spec.tiles[tname], lay = findLayer(tname, 4);
            if (!lay) throw new Error('room.addFromJSON: no tile layer named ' + tname + ' in the template');
            var want = lay[GD.layerTileLen] || ((lay[GD.layerTileData] || []).length);
            if (Array.isArray(arr) && arr.length && typeof arr[0] === 'string') arr = _gridRowsToCells(arr, tname, lay);
            if (!Array.isArray(arr) || arr.length !== want)
              throw new Error('room.addFromJSON: tiles[' + tname + '] needs exactly ' + want +
                ' ints (got ' + (Array.isArray(arr) ? arr.length : typeof arr) + ')');
            for (var ti = 0; ti < arr.length; ti++)
              if (typeof arr[ti] !== 'number' || arr[ti] < 0 || arr[ti] !== (arr[ti] | 0))
                throw new Error('room.addFromJSON: tiles[' + tname + '] must be whole numbers >= 0');
            lay[GD.layerTileData] = arr.slice();
          }
        }
        if (spec.sprites !== undefined) {
          if (!spec.sprites || typeof spec.sprites !== 'object' || Array.isArray(spec.sprites))
            throw new Error('room.addFromJSON: "sprites" must be an object of { layerName: [elements] }');
          for (var sname in spec.sprites) {
            if (!Object.prototype.hasOwnProperty.call(spec.sprites, sname)) continue;
            var elems = spec.sprites[sname], slay = findLayer(sname, 3);
            if (!slay) throw new Error('room.addFromJSON: no sprite layer named ' + sname + ' in the template');
            if (!Array.isArray(elems)) throw new Error('room.addFromJSON: sprites[' + sname + '] must be an array');
            for (var si = 0; si < elems.length; si++)
              if (!elems[si] || typeof elems[si] !== 'object')
                throw new Error('room.addFromJSON: sprites[' + sname + '][' + si + '] must be an object');
            slay[GD.layerSprites] = elems.map(function (el, k) { return _friendlySprite(el, sname, k, spec.variations); });
            slay[GD.layerSpriteCount] = elems.length;
          }
        }
        log('[modkit] room.addFromJSON: ' + spec.name + ' built from room JSON' +
          (spec.tiles ? ' (+tile layers)' : '') + (spec.sprites ? ' (+sprite layers)' : ''));
        return idx;
      },
      addRoom: function (opts) {
        if (!opts || typeof opts !== 'object') throw new Error('addRoom: options object required');
        if (typeof opts.name !== 'string' || !opts.name.length) throw new Error('addRoom: name required');
        var roomsTable = gamedata[GD.rooms];
        if (findIdx(roomsTable, opts.name) >= 0) throw new Error('addRoom: room named ' + opts.name + ' already exists');

        var roomList = window[EN.roomList];
        var booted = (roomList && roomList[EN.roomListArr]) ? true : false;
        var listArr = booted ? roomList[EN.roomListArr] : null;
        if (booted && roomsTable.length !== listArr.length)
          throw new Error('addRoom: gamedata room table out of sync with the engine room list');
        var orderArr = booted ? roomList[EN.roomOrderArr] : null;
        var baseId;
        if (opts.base === undefined) {
          baseId = booted ? orderArr[0]
            : gamedata[GD.roomOrderList][0];
        } else {
          baseId = (typeof opts.base === 'number') ? opts.base : roomIdx(opts.base);
        }
        var template = roomsTable[baseId];
        if (!template) throw new Error('addRoom: template room not found');

        var clearTiles = opts.clearTiles;
        if (clearTiles === undefined) clearTiles = false;
        if (clearTiles !== false && clearTiles !== true && !Array.isArray(clearTiles))
          throw new Error('addRoom: clearTiles must be true, false, or an array of layer names');
        var clearSprites = opts.clearSprites;
        if (clearSprites === undefined) clearSprites = false;
        if (clearSprites !== false && clearSprites !== true && !Array.isArray(clearSprites))
          throw new Error('addRoom: clearSprites must be true, false, or an array of layer names');
        var width = (opts.width === undefined) ? template.width : opts.width;
        var height = (opts.height === undefined) ? template.height : opts.height;
        var record = {
          name: opts.name,
          width: width, height: height,
          speed: (opts.speed === undefined) ? template.speed : opts.speed,
          _C6: (opts.background === undefined) ? template[GD.roomBgColor] : opts.background,
          _D6: template[GD.roomLayerFlag],
          _E6: !!opts.persistent,
          _F6: template._F6,
          _G6: clonePlain(template[GD.roomBackgrounds]),
          _H6: _cloneViews(template[GD.roomViews], width, height),
          instances: [],
          _N6: [],
          layers: []
        };
        if (opts.creationCode) {
          if (typeof opts.creationCode !== 'function') throw new Error('addRoom: creationCode must be a function');
          record[GD.roomCreateCode] = opts.creationCode;
        }

        var layers = clonePlain(template[GD.roomLayers]) || [];
        var instLayer = null;
        for (var li = 0; li < layers.length; li++) {
          if (layers[li].type === 2) {
            if (!instLayer) instLayer = layers[li];
            layers[li][GD.layerInstanceIds] = [];
            layers[li][GD.layerInstanceCount] = 0;
          } else if (layers[li].type === 1 && opts.background !== undefined) {

            layers[li][GD.layerColour] = (opts.background > 0xFFFFFF)
              ? opts.background : (opts.background | 0xFF000000);
          } else if (layers[li].type === 3 && clearSprites !== false) {
            var doClearSpr = (clearSprites === true) ||
              (clearSprites.indexOf(layers[li][GD.name]) >= 0);
            if (doClearSpr) {
              layers[li][GD.layerSprites] = [];
              layers[li][GD.layerSpriteCount] = 0;
            }
          } else if (layers[li].type === 4 && clearTiles !== false) {

            var doClear = (clearTiles === true) ||
              (clearTiles.indexOf(layers[li][GD.name]) >= 0);
            if (doClear) {
              var tlen = layers[li]._S6 || (layers[li]._T6 * layers[li]._U6) || 0;
              var blank = new Array(tlen);
              for (var ti = 0; ti < tlen; ti++) blank[ti] = 0;
              layers[li][GD.layerTileData] = blank;
            }
          }
        }
        if (!instLayer) throw new Error('addRoom: template room has no instance layer');
        record[GD.roomLayers] = layers;

        var insts = opts.instances || [];
        for (var ii = 0; ii < insts.length; ii++) {
          var spec = insts[ii];
          if (spec.object === undefined || spec.x === undefined || spec.y === undefined)
            throw new Error('addRoom: instances[' + ii + '] needs object, x and y');
          var objIndex = (typeof spec.object === 'number') ? spec.object : objIdx(spec.object);
          var objEntry = gamedata[GD.objects][objIndex];
          if (objEntry && objEntry.persistent && opts.allowPersistent !== true) {
            log('[modkit] addRoom: skipping persistent ' + objEntry[GD.name] + ' in ' + opts.name +
                ' (it carries over into every room by itself; pass allowPersistent: true to place one anyway)');
            continue;
          }
          var inst = {
            x: spec.x, y: spec.y, index: objIndex,
            id: _nextRoomInstId++,
            rotation: (spec.rotation === undefined) ? 0 : spec.rotation,
            scaleX: (spec.scaleX === undefined) ? (spec.scale ? spec.scale[0] : 1) : spec.scaleX,
            scaleY: (spec.scaleY === undefined) ? (spec.scale ? spec.scale[1] : 1) : spec.scaleY,
            imageSpeed: (spec.imageSpeed === undefined) ? 1 : spec.imageSpeed,
            imageIndex: (spec.imageIndex === undefined) ? 0 : spec.imageIndex,
            _C6: 4294967295
          };
          if (spec.creationCode) {
            if (typeof spec.creationCode !== 'function') throw new Error('addRoom: instances[' + ii + '].creationCode must be a function');
            inst[GD.instCreationCode] = spec.creationCode;
          }
          record[GD.roomInstances].push(inst);
          instLayer[GD.layerInstanceIds].push(inst.id);
          instLayer[GD.layerInstanceCount] = instLayer[GD.layerInstanceIds].length;
        }

        var id = roomsTable.length;
        roomsTable.push(record);
        if (!booted) {

          gamedata[GD.roomOrderList].push(id);
          return id;
        }
        var roomObj = new (window[EN.roomClass])();
        if (roomObj.id !== id) roomObj.id = id;
        roomObj[GD.roomRaw] = record;
        roomList[EN.roomListPush](roomObj);
        orderArr.push(id);
        roomObj[GD.roomOrderPos] = orderArr.length - 1;
        return id;
      },

      onRoomStart: function (match, fn) {
        if (typeof fn !== 'function') { fn = match; match = undefined; }
        if (typeof fn !== 'function') throw new Error('onRoomStart: callback must be a function');
        var rec = { match: match, fn: fn };
        roomHooks.start.push(rec);
        return { remove: function () { var i = roomHooks.start.indexOf(rec); if (i < 0) return false; roomHooks.start.splice(i, 1); return true; } };
      },

      onRoomEnd: function (match, fn) {
        if (typeof fn !== 'function') { fn = match; match = undefined; }
        if (typeof fn !== 'function') throw new Error('onRoomEnd: callback must be a function');
        var rec = { match: match, fn: fn };
        roomHooks.end.push(rec);
        return { remove: function () { var i = roomHooks.end.indexOf(rec); if (i < 0) return false; roomHooks.end.splice(i, 1); return true; } };
      },

      describe: _describe,

      resources: _resources,

      tables: _tables,

      ask: function (spec) { return npcAsk(spec); },
      askText: function (spec) { return npcAskText(spec); },
      showDialog: function (lines, opts) {
        if (!Array.isArray(lines)) throw new Error('showDialog: lines must be an array');
        if (dialog) _dialogClose(true);
        return _dialogOpen(lines, opts);
      },

      hideDialog: function () { _dialogClose(true); },

      isDialogOpen: function () { return !!dialog; },

      addSaveVar: function (name) {
        if (typeof name !== 'string' || !name.length) throw new Error('addSaveVar: name must be a non-empty string');

        window[EN.saveVarMap][name] = name;
      },

      onGameStart: function (fn) {
        if (typeof fn !== 'function') throw new Error('onGameStart: callback must be a function');
        startHooks.push(fn);
      },

      wait: function (seconds, fn) {
        if (typeof seconds !== 'number' || !(seconds >= 0)) throw new Error('wait: seconds must be a number >= 0');
        if (typeof fn !== 'function') throw new Error('wait: callback must be a function');
        var id = ++waitSeq;
        waitTimers.push({ id: id, remaining: seconds, fn: fn });
        return id;
      },

      cancelWait: function (id) {
        for (var i = 0; i < waitTimers.length; i++) {
          if (waitTimers[i].id === id) { waitTimers.splice(i, 1); return true; }
        }
        return false;
      },

      settingsAdd: function (opts) { return _registerModSetting(manifest, opts); },
      menuAddRow: function (spec) { return _menuAddRow(manifest, spec); },
      _mouseDebug: _mouseDebug,
      menuAddTitle: function (spec) { return _menuAddText(manifest, spec, 'title'); },
      menuAddSubtitle: function (spec) { return _menuAddText(manifest, spec, 'subtitle'); },
      menuAddText: function (spec) { return _menuAddText(manifest, spec, 'text'); },
      inventoryAdd: function (opts) { return _inventoryAdd(opts); },
      inventoryList: function () { return _inventoryList(); },
      inventoryRemove: function (id) { return _inventoryRemove(id); },
      saveGiveItem: function (item, target) {
        var k = _saveItemName(item);
        var had = _invOwned(k);
        if (target === 'temp') _sessionSet(k, 1); else _saveIniWrite(k, 1);
        if (!had && target !== 'temp' && _gameStepCount > 0 && manifest.itemToast !== false) _itemToast(k);
        return true;
      },
      saveTakeItem: function (item, target) {
        var k = _saveItemName(item);
        if (target === 'temp') _sessionSet(k, 0); else _saveIniWrite(k, 0);
        return true;
      },
      saveHasItem: function (item) {
        var k = _saveItemName(item);
        var s = _sessionGet(k);
        if (s !== undefined && Number(s) > 0) return true;
        return Number(_saveIniRead(k, 0)) > 0.5;
      },
      saveListItems: function () {
        var out = [];
        for (var i = 0; i < _KNOWN_ITEMS.length; i++) {
          var k = _KNOWN_ITEMS[i];
          var s = _sessionGet(k);
          var owned = (s !== undefined && Number(s) > 0) || Number(_saveIniRead(k, 0)) > 0.5;
          out.push({ item: k, owned: owned });
        }
        return out;
      },
      saveSetFlag: function (key, value, target) {
        var k = String(key == null ? '' : key);
        if (!k.length) throw new Error('save.setFlag: key required');
        if (target === 'temp') { _sessionSet(k, value); return true; }
        if (typeof value === 'number' && isFinite(value)) { _saveIniWrite(k, value); return true; }
        _sessionSet(k, value); // non-numeric values stay session-only
        return true;
      },
      saveGetFlag: function (key, def) {
        var k = String(key == null ? '' : key);
        var s = _sessionGet(k);
        if (s !== undefined) return s;
        return _saveIniRead(k, def === undefined ? 0 : def);
      },
      settingsRemove: function (id) {
        for (var i = 0; i < _settingsRegistry.length; i++)
          if (_settingsRegistry[i].mod === manifest.name && _settingsRegistry[i].id === id) {
            _settingsRegistry.splice(i, 1);
            log('[modkit] ' + manifest.name + ': setting "' + id + '" removed');
            return true;
          }
        return false;
      },
      settingsList: function () {
        return _settingsForMod(manifest.name).map(function (e) {
          return { id: e.id, label: e.label, type: e.type, value: e.value, min: e.min, max: e.max };
        });
      },
      // documented in api.json; the console object (window.ModKit) owns the data, mods get the same records
      list: function () { return (window.ModKit && typeof window.ModKit.list === 'function') ? window.ModKit.list() : []; },
      _settingsForMod: _settingsForMod,
      _instance: function (id) { return resolveInstance(null, null, id); },

      log: function () {
        var logArgs = Array.prototype.slice.call(arguments);
        log.apply(null, ['[mod:' + manifest.name + ']'].concat(logArgs));
      }
    };
    _addRoomRef = api.addRoom;
    return api;
  }

  function gameFPS() {
    try {
      if (window[EN.useTimeSource]) {
        var ts = window[EN.timeSource];
        var f = (ts && ts[EN.timeSourceFps]) ? ts[EN.timeSourceFps]() : 0;
        if (f > 0) return f;
      }
      var rm = window[EN.roomManager];
      var r = (rm && rm[EN.roomSpeedGet]) ? rm[EN.roomSpeedGet]() : 0;
      return r > 0 ? r : 60;
    } catch (e) { return 60; }
  }

  function stepTimers(dt) {
    var due = [], still = [];
    for (var i = 0; i < waitTimers.length; i++) {
      var t = waitTimers[i];
      t.remaining -= dt;
      if (t.remaining <= 0) due.push(t); else still.push(t);
    }
    waitTimers = still;
    for (var j = 0; j < due.length; j++) {
      try { due[j].fn(); }
      catch (err) { loud('wait() callback failed', err); }
    }
  }

  var controllerTickInstalled = false;
  function installControllerTick() {
    if (controllerTickInstalled) return;
    controllerTickInstalled = true;
    var controllerIdx = findIdx(gamedata[GD.objects], 'obj_Controller');
    if (controllerIdx < 0) { log('[modkit] WARNING: obj_Controller not found; onGameStart hooks and wait() timers will not run'); return; }
    var controllerEntry = gamedata[GD.objects][controllerIdx];
    var orig = controllerEntry.ev_Step || null;
    var origDraw = controllerEntry.ev_Draw || null;
    var fired = false;
    controllerEntry.ev_Step = function (self, other) {
      if (!fired) {
        fired = true;
        for (var i = 0; i < startHooks.length; i++) {
          try { startHooks[i](self, other); }
          catch (err) { loud('onGameStart hook failed', err); }
        }
      }
      _gameStepCount++;
      if (_gameStepCount === 10) { try { _engineMapCheck(true); } catch (e) { } }
      stepTimers(1 / gameFPS());
      var r = orig ? orig(self, other) : undefined;
      // click handling runs AFTER the stock controller Step: simulated keys
      // (pressed_space etc.) would be remapped back to 0 by the stock
      // per-frame key sync otherwise, and the player Steps later in the
      // same frame, so the key still reaches it this frame.
      try {
        var held = yyGetBool(mouse_check_button(1));
        if (!held && window._Lw && window._Lw._Uu) held = !!window._Lw._Uu[0];
        if (held && !_mousePrevHeld) {
          var mx = mouse_x(0), my = mouse_y(0);
          for (var zi = 0; zi < _mouseZones.length; zi++) {
            var z = _mouseZones[zi];
            if (mx >= z.x && mx < z.x + z.w && my >= z.y && my < z.y + z.h) {
              try { z.action(); } catch (zerr) { loud('mouse zone action failed', zerr); }
              break;
            }
          }
        }
        _mousePrevHeld = held;
        // mouse wheel: scrolls the inventory bar (left/right) or the open menus (up/down)
        if (_wheelAcc !== 0) {
          var wd = _wheelAcc > 0 ? 1 : -1;
          _wheelAcc = 0;
          if (!(dialog && !dialog.stock) && !yyGetBool(instance_exists(67))) {
            var ttl = findIdx(gamedata[GD.objects], 'obj_Title');
            if (yyGetBool(instance_exists(28))) { if (wd > 0) self.pressed_right = 1; else self.pressed_left = 1; }
            else if (yyGetBool(instance_exists(49)) || (ttl >= 0 && yyGetBool(instance_exists(ttl)))) {
              if (wd > 0) self.pressed_down = 1; else self.pressed_up = 1;
            }
          }
        }
        // pointer cursor while the mouse is over any clickable zone
        var overZone = false;
        for (var hz = 0; hz < _mouseZones.length && !overZone; hz++) {
          var hzz = _mouseZones[hz], hmx = mouse_x(0), hmy = mouse_y(0);
          overZone = hmx >= hzz.x && hmx < hzz.x + hzz.w && hmy >= hzz.y && hmy < hzz.y + hzz.h;
        }
        _setPointerCursor(overZone);
        _mouseZones.length = 0;
      } catch (merr) { /* mouse IO not ready yet */ }
      try { dialogTick(self); }
      catch (err) { loud('dialog tick failed (dialog closed, player restored)', err); _dialogAbort(); }
      // while a mod dialog / input box is open (and for two frames after it closes) nothing
      // else may react to the shared key flags: the pause menu and HUD would take the same
      // Z / X / arrows the dialog uses
      if ((dialog && !dialog.stock) || _inputGrace > 0) {
        if (_inputGrace > 0) _inputGrace--;
        for (var kk = 0; kk < _DIALOG_KEYS.length; kk++) self[_DIALOG_KEYS[kk]] = 0;
      }
      try { _levelTick(); }
      catch (err) { loud('level tick failed', err); }
      return r;
    };

    if (origDraw) controllerEntry.ev_Draw = origDraw;
  }

// ===== menu hooks registry + mouse zones + save editing ================
  var _menuHooks = { rows: [], texts: [] };
  function _menuWhich(spec) {
    return spec.menu === 'title' ? 'title' : spec.menu === 'pause' ? 'pause' : 'both';
  }
  function _menuAddRow(manifest, spec) {
    if (!spec || typeof spec !== 'object') throw new Error('menu.addRow: spec object required');
    var label = String(spec.label || '');
    if (!label.length) throw new Error('menu.addRow: label required');
    if (typeof spec.onSelect !== 'function') throw new Error('menu.addRow: onSelect must be a function');
    var rec = { owner: manifest ? manifest.name : '?', menu: _menuWhich(spec), label: label, onSelect: spec.onSelect };
    _menuHooks.rows.push(rec);
    return { remove: function () { var i = _menuHooks.rows.indexOf(rec); if (i >= 0) _menuHooks.rows.splice(i, 1); } };
  }
  function _menuAddText(manifest, spec, kind) {
    if (!spec || typeof spec !== 'object') throw new Error('menu.add' + kind + ': spec object required');
    var text = String(spec.text || '');
    if (!text.length) throw new Error('menu.add' + kind + ': text required');
    var rec = { owner: manifest ? manifest.name : '?', menu: _menuWhich(spec), kind: kind, text: text };
    _menuHooks.texts.push(rec);
    return { remove: function () { var i = _menuHooks.texts.indexOf(rec); if (i >= 0) _menuHooks.texts.splice(i, 1); } };
  }
  function _menuRowsFor(menu) {
    var out = [];
    for (var i = 0; i < _menuHooks.rows.length; i++) {
      var r = _menuHooks.rows[i];
      if (r.menu === menu || r.menu === 'both') out.push(r);
    }
    return out;
  }
  function _menuTextsFor(menu) {
    var out = [];
    for (var i = 0; i < _menuHooks.texts.length; i++) {
      var r = _menuHooks.texts[i];
      if (r.menu === menu || r.menu === 'both') out.push(r);
    }
    return out;
  }

  // mouse zones: pushed during Draw events, consumed + cleared by the
  // controller Step each frame (one-frame lag is fine for clicks)
  var _mouseZones = [];
  var _mousePrevHeld = false;
  var _wheelAcc = 0;
  try { window.addEventListener('wheel', function (ev) { _wheelAcc += ev.deltaY > 0 ? 1 : (ev.deltaY < 0 ? -1 : 0); }, { passive: true }); } catch (e) { }
  // clickable width of a one-line menu row: text length * ~4px (font 0 measured ~3.6px per glyph) (font 0) + padding, capped
  function _zw(text, pad, cap) { return Math.min(cap, pad + String(text).length * 4); }
  function _mouseIn(x, y, w, h) {
    try {
      var mx = mouse_x(0), my = mouse_y(0);
      return mx >= x && mx < x + w && my >= y && my < y + h;
    } catch (e) { return false; }
  }
  var _cursorPointer = false;
  function _setPointerCursor(on) {
    if (on === _cursorPointer) return;
    _cursorPointer = on;
    try {
      var cv = document.getElementById('canvas') || document.querySelector('canvas');
      if (cv) cv.style.cursor = on ? 'pointer' : '';
    } catch (e) { }
  }
  function _mouseDebug() { return { zones: _mouseZones.length, held: _mousePrevHeld }; }

  // ---- save editing -----------------------------------------------------
  // The game's real save system is save.ini [save1]: every progress flag and
  // item (rope, ropeWell, lever, clue<idx>...) is a direct ini key. The
  // map_save / map_save_temp / map_load scripts are DEAD CODE (no call
  // sites, no tempSave field anywhere), so the kit's 'temp' target is an
  // in-memory session overlay: visible to save.hasItem / save.getFlag for
  // this page load, never written to the ini.
  var _sessionFlags = {};
  var _KNOWN_ITEMS = ['rope', 'plunger', 'lighter', 'shovel'];
  function _saveIniRead(key, def) {
    ini_open('save.ini'); var v = ini_read_real('save1', key, def); ini_close(); return v;
  }
  function _saveIniWrite(key, val) {
    ini_open('save.ini'); ini_write_real('save1', key, val); ini_close();
  }
  function _sessionSet(key, val) { _sessionFlags[key] = val; }
  function _sessionGet(key) {
    return Object.prototype.hasOwnProperty.call(_sessionFlags, key) ? _sessionFlags[key] : undefined;
  }

  function _saveItemName(item) {
    var s = String(item == null ? '' : item).replace(/^\$/, '');
    if (!s.length) throw new Error('save API: item name required');
    return s;
  }
function makeAPI(manifest, files) {
  var a = _makeAPI(manifest, files);
  var cls = {
    npc: { ask: a.ask, askText: a.askText, showDialog: a.showDialog,
           hideDialog: a.hideDialog, isDialogOpen: a.isDialogOpen },
    room: { add: a.addRoom, addFromJSON: a.addRoomFromJSON, addInstance: a.addInstance,
            remove: a.removeRoom, removeInstance: a.removeRoomInstance, forceNext: a.forceNextRoom,
            current: a.currentRoom, onStart: a.onRoomStart, onEnd: a.onRoomEnd,
            patch: a.patchRoom, list: a.listRooms, name: a.roomName, idx: a.room },
    sprite: { add: a.addSprite, addData: a.addSpriteData, addFile: a.addSpriteFile,
              override: a.overrideSprite, draw: a.drawImage, list: a.listSprites,
              name: a.spriteName, idx: a.sprite },
    sound: { add: a.addSound, play: a.playSound, list: a.listSounds,
             name: a.soundName, idx: a.sound },
    object: { add: a.addObject, list: a.listObjects, name: a.objectName,
              wrapEvent: a.wrapEvent, unwrapEvent: a.unwrapEvent, eventSource: a.eventSource, patch: a.patchObject,
              idx: a.obj },
    clue: { add: a.addClue, list: a.listClues, found: a.clueFound,
            remove: a.removeClue, clear: a.clearClues, nearest: a.nearestClue },
    menu: { setBackground: a.setMenuBackground, clear: a.clearMenuBackground,
            addRow: a.menuAddRow, addTitle: a.menuAddTitle,
            addSubtitle: a.menuAddSubtitle, addText: a.menuAddText },
    save: { giveItem: a.saveGiveItem, takeItem: a.saveTakeItem, hasItem: a.saveHasItem,
            listItems: a.saveListItems, setFlag: a.saveSetFlag, getFlag: a.saveGetFlag },
    inventory: { add: a.inventoryAdd, remove: a.inventoryRemove, list: a.inventoryList },
    settings: { add: a.settingsAdd, remove: a.settingsRemove, list: a.settingsList },
    view: { pos: a.viewPos }
  };
  var moved = ['removeRoom','removeRoomInstance','saveGiveItem','saveTakeItem','saveHasItem','saveListItems','saveSetFlag','saveGetFlag',
    'menuAddRow','menuAddTitle','menuAddSubtitle','menuAddText','ask','askText','showDialog','hideDialog','isDialogOpen','addRoom','addRoomFromJSON','addInstance',
    'forceNextRoom','currentRoom','onRoomStart','onRoomEnd','patchRoom','listRooms','roomName','room',
    'addSprite','addSpriteData','addSpriteFile','overrideSprite','drawImage','listSprites','spriteName','sprite',
    'addSound','playSound','listSounds','soundName','sound','addObject','listObjects','objectName',
    'wrapEvent','eventSource','patchObject','obj','addClue','listClues','clueFound','clearClues',
    'nearestClue','inventoryAdd','inventoryList','inventoryRemove','removeClue','unwrapEvent','settingsRemove','setMenuBackground','clearMenuBackground','settingsAdd','settingsList','viewPos'];
  for (var i = 0; i < moved.length; i++) delete a[moved[i]];
  for (var k in cls) if (cls.hasOwnProperty(k)) a[k] = cls[k];
  return a;
}


  // -------------------------------------------------------------------
  // LEVEL PACKS - manifest-declared rooms with an optional "pattern
  // entrance" (the SecretGrotto trick, done by the kit instead of by hand).
  //
  //   "levels": [{
  //     "id": "cavern", "room": "data/cavern.jsonc",
  //     "entrance": { "pattern": ["N","N","W","S"] },
  //     "exit": { "side": "W", "margin": 20, "band": [100, 200], "to": "origin", "grace": 150 },
  //     "once": false
  //   }]
  //
  // - the room file is the same shape room.addFromJSON takes, read from the
  //   pack zip/folder; rooms are built right after init.js (so the objects
  //   and sprites it defined exist) and BEFORE any "scripts" entries (so
  //   those can place clues/NPCs by room name).
  // - entrance.pattern: committed room exits (N/S/E/W) counted across the
  //   whole world. When all but the last exit have been walked, the kit arms a
  //   one-shot reroute for the last direction (same engine as
  //   room.forceNext); taking that exit lands in the level.
  // - exit: walking into the given screen edge (after a grace period) fades
  //   back to the room the player came from ("origin") or a named room.
  // - once: the level is removed (room.remove) after the player leaves it.
  var _levels = [];
  var _levelLastLive = null;
  var _levelOrigin = null;
  var _levelLastTrans = 0;
  var _LEVEL_SIDE = { 0: 'N', 1: 'S', 2: 'W', 3: 'E' };
  var _LEVEL_DIRNAME = { N: 'north', S: 'south', E: 'east', W: 'west' };

  function _levelSide(raw, where) {
    var s = String(raw === undefined || raw === null ? '' : raw).trim().toUpperCase();
    if (s === 'NORTH' || s === 'UP') s = 'N';
    else if (s === 'SOUTH' || s === 'DOWN') s = 'S';
    else if (s === 'EAST' || s === 'RIGHT') s = 'E';
    else if (s === 'WEST' || s === 'LEFT') s = 'W';
    if (s !== 'N' && s !== 'S' && s !== 'E' && s !== 'W')
      throw new Error(where + ': "' + raw + '" is not a direction (use N, S, E or W)');
    return s;
  }

  function _levelByRoom(roomId) {
    for (var i = 0; i < _levels.length; i++)
      if (_levels[i].roomId === roomId && !_levels[i].spent) return _levels[i];
    return null;
  }
  function _anyLevelRoom(roomId) {
    for (var i = 0; i < _levels.length; i++) if (_levels[i].roomId === roomId) return true;
    return false;
  }

  function _levelParse(manifest, spec, index) {
    var where = manifest.name + ' levels[' + index + ']';
    if (!spec || typeof spec !== 'object' || Array.isArray(spec))
      throw new Error(where + ': each level must be an object');
    if (typeof spec.id !== 'string' || !/^[A-Za-z0-9_\-]+$/.test(spec.id))
      throw new Error(where + ': "id" (letters, digits, _ and -) required');
    if (typeof spec.room !== 'string' || !spec.room.length)
      throw new Error(where + ' (' + spec.id + '): "room" (pack-relative path to the room JSON/JSONC) required');
    var lv = {
      mod: manifest.name, id: spec.id, file: spec.room, title: spec.title || spec.id,
      roomId: -1, pattern: null, once: !!spec.once,
      exit: { side: 'W', margin: 20, band: null, to: 'origin', grace: 150 },
      spent: false, progress: 0, armEntry: null, graceFrames: 0, exitArmed: false, leaving: false
    };
    var ent = spec.entrance;
    if (ent !== undefined && ent !== null) {
      if (typeof ent !== 'object' || !Array.isArray(ent.pattern) || ent.pattern.length < 2)
        throw new Error(where + ' (' + spec.id + '): entrance.pattern must be an array of at least 2 directions (N/S/E/W)');
      lv.pattern = ent.pattern.map(function (d, di) { return _levelSide(d, where + ' entrance.pattern[' + di + ']'); });
    }
    var ex = spec.exit;
    if (ex !== undefined && ex !== null) {
      if (typeof ex !== 'object' || Array.isArray(ex)) throw new Error(where + ' (' + spec.id + '): "exit" must be an object');
      if (ex.side !== undefined) lv.exit.side = _levelSide(ex.side, where + ' exit.side');
      if (ex.margin !== undefined) {
        if (typeof ex.margin !== 'number' || !(ex.margin > 0)) throw new Error(where + ' exit.margin must be a number > 0');
        lv.exit.margin = ex.margin;
      }
      if (ex.band !== undefined && ex.band !== null) {
        if (!Array.isArray(ex.band) || ex.band.length !== 2 || typeof ex.band[0] !== 'number' || typeof ex.band[1] !== 'number' || ex.band[0] >= ex.band[1])
          throw new Error(where + ' exit.band must be [low, high] along the edge (numbers, low < high)');
        lv.exit.band = [ex.band[0], ex.band[1]];
      }
      if (ex.to !== undefined) {
        if (typeof ex.to !== 'string' || !ex.to.length) throw new Error(where + ' exit.to must be "origin" or a room name');
        lv.exit.to = ex.to;
      }
      if (ex.grace !== undefined) {
        if (typeof ex.grace !== 'number' || ex.grace < 0) throw new Error(where + ' exit.grace must be a number of steps >= 0');
        lv.exit.grace = ex.grace;
      }
    }
    return lv;
  }

  // Called from runMod, between init.js and the "scripts" entries.
  function _setupLevels(manifest, files, api) {
    var specs = manifest.levels;
    if (specs === undefined || specs === null) return;
    if (!Array.isArray(specs)) throw new Error('manifest "levels" must be an array');
    var seen = {};
    for (var i = 0; i < specs.length; i++) {
      var lv = _levelParse(manifest, specs[i], i);
      if (seen[lv.id]) throw new Error(manifest.name + ' levels: duplicate id "' + lv.id + '"');
      seen[lv.id] = true;
      if (!files[lv.file]) throw new Error(manifest.name + ' levels (' + lv.id + '): missing room file ' + lv.file);
      lv.roomId = api.room.addFromJSON(decodeText(files[lv.file]));
      _levels.push(lv);
      log('[modkit] ' + manifest.name + ': level "' + lv.id + '" -> room ' + lv.roomId +
        ' (' + gamedata[GD.rooms][lv.roomId][GD.name] + ')' +
        (lv.pattern ? ', entrance ' + lv.pattern.join(',') : ', no entrance pattern (route it from a script)'));
    }
  }

  function _levelDisarm(lv) {
    if (!lv.armEntry) return;
    var at = forcedRooms.indexOf(lv.armEntry);
    if (at >= 0) forcedRooms.splice(at, 1);
    lv.armEntry = null;
  }
  function _levelDisarmAll() { for (var i = 0; i < _levels.length; i++) _levelDisarm(_levels[i]); }

  // Each pattern level keeps just ONE number: how many of its exits in a row the
  // player has walked (lv.progress). A committed exit either advances it, or
  // restarts it (to 1 when the exit happens to be the pattern's first step, else
  // 0) - nothing is buffered and nothing is re-scanned. progress === length-1
  // means "armed": one more exit in the last direction opens the level.
  function _levelAdvance(lv, side) {
    if (side === lv.pattern[lv.progress]) lv.progress++;
    else lv.progress = (side === lv.pattern[0]) ? 1 : 0;
    // the closing exit was taken without the reroute (e.g. another mod's route won)
    if (lv.progress >= lv.pattern.length) lv.progress = (side === lv.pattern[0]) ? 1 : 0;
  }

  function _levelSyncArm(lv) {
    var armed = lv.progress === lv.pattern.length - 1;
    if (armed && !lv.armEntry) {
      lv.armEntry = { roomId: lv.roomId, dirs: [_LEVEL_DIRNAME[lv.pattern[lv.pattern.length - 1]]],
        any: false, owner: 'level:' + lv.mod + '/' + lv.id };
      forcedRooms.push(lv.armEntry);
      _installRoomGotoHook();
      log('[modkit] level "' + lv.id + '": armed - one more ' + lv.pattern[lv.pattern.length - 1] + ' exit opens it');
    } else if (!armed) _levelDisarm(lv);
  }

  // One committed room exit (N/S/E/W). Exposed for tests as ModKit._levelFeed.
  function _levelFeedExit(side) {
    for (var i = 0; i < _levels.length; i++) {
      var lv = _levels[i];
      if (!lv.pattern || lv.spent) { _levelDisarm(lv); continue; }
      _levelAdvance(lv, side);
      _levelSyncArm(lv);
    }
  }

  // a live instance of an object, or null (resolveInstance on "no instance" is an engine error)
  function _levelFirst(objIdx) {
    if (objIdx < 0 || !instance_exists(objIdx)) return null;
    var inst = resolveInstance(null, null, instance_find(objIdx, 0));
    return (inst && typeof inst === 'object') ? inst : null;
  }

  function _levelGoBack(lv, self) {
    var target;
    if (lv.exit.to === 'origin') {
      target = _levelOrigin;
      if (target === null || target === undefined) {
        target = findIdx(gamedata[GD.rooms], 'rm_Statue');
        if (target < 0) target = 0;
      }
    } else {
      target = findIdx(gamedata[GD.rooms], lv.exit.to);
      if (target < 0) { loud('level "' + lv.id + '": exit.to room not found: ' + lv.exit.to, new Error('no such room')); lv.exit.to = 'origin'; return; }
    }
    lv.leaving = true;
    var rhIdx = findIdx(gamedata[GD.objects], 'obj_RoomHandler');
    var transIdx = findIdx(gamedata[GD.objects], 'obj_RoomTransition');
    var rh = _levelFirst(rhIdx);
    if (rh) rh.prevExit = 10;
    var trans = gml_Script_instance_create(self, self, 0, 0, transIdx);
    var transInst = (trans === -4 || trans < 0) ? null : resolveInstance(self, self, trans);
    if (transInst) transInst.goto = target;
    var con = _levelFirst(33);
    if (con) con.pressed_Z = 0;
    log('[modkit] level "' + lv.id + '": leaving to room ' + target);
  }

  function _levelRoomChanged(prev, live) {
    var left = (prev !== null) ? _levelByRoom(prev) : null;
    if (prev !== null && !_anyLevelRoom(prev)) _levelOrigin = prev;
    if (left && left.once && !left.spent) {
      left.spent = true;
      _levelDisarm(left);
      var rec = gamedata[GD.rooms][left.roomId];
      if (rec && !rec.__modkitRemoved) _retireRoom(left.roomId);
      log('[modkit] level "' + left.id + '" was one-time - removed');
    }
    var entered = (live !== null) ? _levelByRoom(live) : null;
    // pattern progress lives in each level and survives ordinary room changes
    // (every exit of a pattern changes the room); entering a level resets it
    if (entered) {
      for (var li = 0; li < _levels.length; li++) _levels[li].progress = 0;
      _levelDisarmAll();
      entered.graceFrames = 0;
      entered.exitArmed = false;
      entered.leaving = false;
    }
  }

  function _levelTick() {
    if (!_levels.length) return;
    var live = _liveRoomId();
    if (live === null) return;
    if (live !== _levelLastLive) {
      var prev = _levelLastLive;
      _levelLastLive = live;
      _levelRoomChanged(prev, live);
    }
    var transIdx = findIdx(gamedata[GD.objects], 'obj_RoomTransition');
    var rhIdx = findIdx(gamedata[GD.objects], 'obj_RoomHandler');
    var transCount = (transIdx >= 0) ? instance_number(transIdx) : 0;
    if (transCount > _levelLastTrans && transCount === 1 && !_anyLevelRoom(live)) {
      var rh = _levelFirst(rhIdx);
      var side = rh && _LEVEL_SIDE[rh.prevExit];
      if (side) _levelFeedExit(side);
    }
    _levelLastTrans = transCount;

    var lv = _levelByRoom(live);
    if (!lv || dialog) return;
    var p = _levelFirst(36);
    if (!p || typeof p.x !== 'number' || typeof p.y !== 'number') return;
    var rec = gamedata[GD.rooms][lv.roomId];
    var w = rec.width, h = rec.height;
    p.x = Math.max(8, Math.min(w - 8, p.x));
    p.y = Math.max(8, Math.min(h - 8, p.y));
    if (lv.leaving) return;
    lv.graceFrames++;
    var ex = lv.exit, along, dist;
    if (ex.side === 'W') { dist = p.x; along = p.y; }
    else if (ex.side === 'E') { dist = w - p.x; along = p.y; }
    else if (ex.side === 'N') { dist = p.y; along = p.x; }
    else { dist = h - p.y; along = p.x; }
    var inBand = !ex.band || (along >= ex.band[0] && along <= ex.band[1]);
    if (!lv.exitArmed && lv.graceFrames > ex.grace && dist > ex.margin) lv.exitArmed = true;
    if (lv.exitArmed && dist < ex.margin && inBand) _levelGoBack(lv, p);
  }


  function installModMenu() {
    var titleIdx = findIdx(gamedata[GD.objects], 'obj_Title');
    if (titleIdx < 0) { log('[modkit] obj_Title not found; mod menu not installed'); return; }
    var entry = gamedata[GD.objects][titleIdx];

    function modListData() {
      var disabled = getDisabled();
      var list = [];
      for (var i = 0; i < allMods.length; i++) {
        var modInfo = allMods[i];
        list.push({
          name: modInfo.name,
          title: modInfo.title || modInfo.name,
          version: modInfo.version || '',
          author: modInfo.author || '',
          description: modInfo.description || '',
          icon: (typeof modInfo.__iconSprite === 'number') ? modInfo.__iconSprite : -1,
          enabled: disabled.indexOf(modInfo.name) < 0,
          failed: modInfo.__failed || ''
        });
      }
      return list;
    }

    // ---- per-mod menu: enable toggle + the mod's settings + its console
    // commands (declared in package.jsonc "console", implemented via
    // Commands.<name> in init.js - the menu only calls functions the mod
    // itself registered; it never evaluates strings).
    function commandEntries(modName) {
      var out = [];
      var mod = null;
      for (var i = 0; i < loadedMods.length; i++)
        if (loadedMods[i].name === modName) { mod = loadedMods[i]; break; }
      if (!mod || !mod.console || !mod.__commands) return out;
      var meta = mod.console;
      for (var key in meta)
        if (Object.prototype.hasOwnProperty.call(meta, key)) {
          var fn = (mod.__commands && typeof mod.__commands[key] === 'function')
            ? mod.__commands[key] : null;
          out.push({ kind: 'command', name: key, desc: String(meta[key]), fn: fn });
        }
      return out;
    }

    function modMenuEntries(modName) {
      var list = [{ kind: 'enable' }];
      var set = _settingsForMod(modName);
      for (var i = 0; i < set.length; i++) list.push({ kind: 'setting', entry: set[i] });
      var cmds = commandEntries(modName);
      for (var j = 0; j < cmds.length; j++) list.push(cmds[j]);
      return list;
    }

    function entryLine(ent, modEnabled) {
      if (ent.kind === 'enable') return 'Mod enabled: ' + (modEnabled ? 'ON' : 'OFF');
      if (ent.kind === 'setting') {
        if (ent.entry.type === 'toggle') return ent.entry.label + ': ' + (ent.entry.value ? 'ON' : 'OFF');
        if (ent.entry.type === 'number') return ent.entry.label + ': ' + ent.entry.value;
        return ent.entry.label;
      }
      return '> ' + ent.name;
    }

    var origCreate = entry.ev_Create || null;
    entry.ev_Create = function (self, other) {
      if (origCreate) origCreate(self, other);
      self.cursorIndexMax = 4 + _menuRowsFor('title').length;
      self.modListOpen = 0;
      self.modSel = 0;
      self.modTop = 0;
      self.modData = null;
      self.modToggled = -1;
      self.modMenuOpen = 0;
      self.menuEntries = null;
      self.menuSel = 0;
      self.menuTop = 0;
    };

    var origStep = entry.ev_Step || null;
    entry.ev_Step = function (self, other) {

      var con = null;
      try {
        con = resolveInstance(self, other, (self.con !== undefined) ? self.con : instance_find(33, 0));
      } catch (e) { con = null; }
      if (!con) { if (origStep) return origStep(self, other); return; }

      if (yyGetBool(instance_exists(14))) { if (origStep) return origStep(self, other); return; }

      // ---- per-mod settings/command menu -------------------------------
      if (yyGetBool(self.modMenuOpen)) {
        var entCount = self.menuEntries ? self.menuEntries.length : 0;
        if (yyGetBool(con.pressed_up)) { self.menuSel--; con.pressed_up = 0; audio_play_sound(16, 1, 0); }
        if (yyGetBool(con.pressed_down)) { self.menuSel++; con.pressed_down = 0; audio_play_sound(16, 1, 0); }
        if (entCount > 0) {
          if (yyfgreater(self.menuSel, yyfminus(real(entCount), 0.5))) self.menuSel = 0;
          if (yyfless(self.menuSel, -0.5)) self.menuSel = entCount - 1;
          var pageM = Math.max(1, Math.floor((((_Mb && _Mb.room_height) ? _Mb.room_height : 600) - 150) / 20));
          if (self.menuSel < self.menuTop) self.menuTop = self.menuSel;
          if (self.menuSel > self.menuTop + pageM - 1) self.menuTop = self.menuSel - pageM + 1;
        } else { self.menuSel = 0; self.menuTop = 0; }

        if ((yyGetBool(con.pressed_Z) || yyGetBool(con.pressed_space)) && entCount > 0) {
          var ent = self.menuEntries[self.menuSel];
          if (ent.kind === 'enable') {
            var modInfo = self.modData[self.modSel];
            var disabledList = getDisabled();
            var foundAt = disabledList.indexOf(modInfo.name);
            if (foundAt >= 0) disabledList.splice(foundAt, 1); else disabledList.push(modInfo.name);
            setDisabled(disabledList);
            modInfo.enabled = !modInfo.enabled;
            self.modToggled = self.modSel;
            audio_play_sound(17, 2, 0);
          } else if (ent.kind === 'setting') {
            if (ent.entry.type === 'toggle') _settingToggle(ent.entry);
            else if (ent.entry.type === 'number') _settingAdjust(ent.entry, 1);
            else _settingRun(ent.entry);
            audio_play_sound(17, 2, 0);
          } else if (ent.kind === 'command') {
            if (ent.fn) {
              try { ent.fn(); audio_play_sound(17, 2, 0); }
              catch (err) { loud('menu command "' + ent.name + '" failed', err); }
            }
          }
          con.pressed_Z = 0; con.pressed_space = 0;
        }

        var backM = false;
        if (typeof con.pressed_X !== 'undefined' && yyGetBool(con.pressed_X)) { con.pressed_X = 0; backM = true; }
        if (backM) { self.modMenuOpen = 0; }
        return;
      }

      // ---- installed-mods list -----------------------------------------
      if (yyGetBool(self.modListOpen)) {

        var modCount = self.modData ? self.modData.length : 0;
        if (yyGetBool(con.pressed_up)) { self.modSel--; con.pressed_up = 0; audio_play_sound(16, 1, 0); }
        if (yyGetBool(con.pressed_down)) { self.modSel++; con.pressed_down = 0; audio_play_sound(16, 1, 0); }
        if (modCount > 0) {
          if (yyfgreater(self.modSel, yyfminus(real(modCount), 0.5))) self.modSel = 0;
          if (yyfless(self.modSel, -0.5)) self.modSel = modCount - 1;

          var page = Math.max(1, Math.floor((((_Mb && _Mb.room_height) ? _Mb.room_height : 600) - 150) / 20));
          if (self.modSel < self.modTop) self.modTop = self.modSel;
          if (self.modSel > self.modTop + page - 1) self.modTop = self.modSel - page + 1;
        } else { self.modSel = 0; self.modTop = 0; }

        if ((yyGetBool(con.pressed_Z) || yyGetBool(con.pressed_space)) && modCount > 0) {
          self.menuEntries = modMenuEntries(self.modData[self.modSel].name);
          self.modMenuOpen = 1;
          self.menuSel = 0;
          self.menuTop = 0;
          audio_play_sound(17, 2, 0);
          con.pressed_Z = 0; con.pressed_space = 0;
        }

        var back = false;
        if (typeof con.pressed_X !== 'undefined' && yyGetBool(con.pressed_X)) { con.pressed_X = 0; back = true; }
        if (back) { self.modListOpen = 0; }
        return;
      }

      if ((yyGetBool(con.pressed_Z) || yyGetBool(con.pressed_space)) && self.cursorIndex >= 5) {
        var titleRowsL = _menuRowsFor('title');
        var trec = titleRowsL[self.cursorIndex - 5];
        con.pressed_Z = 0; con.pressed_space = 0;
        if (trec) {
          try { trec.onSelect(); audio_play_sound(17, 2, 0); }
          catch (err) { loud('title menu row "' + trec.label + '" failed', err); }
        }
        return;
      }
      if ((yyGetBool(con.pressed_Z) || yyGetBool(con.pressed_space)) && yyfgreater(self.cursorIndex, 3.5)) {
        con.pressed_Z = 0; con.pressed_space = 0;
        self.modData = modListData();
        self.modListOpen = 1;
        self.modSel = 0;
        self.modTop = 0;
        self.modToggled = -1;
        if (typeof audio_play_sound === 'function') audio_play_sound(17, 2, 0);
        return;
      }
      if (origStep) return origStep(self, other);
    };

    var origDraw = entry.ev_Draw || null;
    entry.ev_Draw = function (self, other) {
      var listOpen = yyGetBool(self.modListOpen);
      var menuOpen = yyGetBool(self.modMenuOpen);

      if (!listOpen && !menuOpen && origDraw) origDraw(self, other);

      window.__modkitMenuDraw = true;

      if (!listOpen && !menuOpen) {
        draw_set_halign(0);
        draw_set_valign(1);
        draw_set_font(0);
        draw_text_outlined(self, other, self.textX,
          yyfplus(yyfplus(real(self.textInitY), real(yyftime(4, real(self.textSpaceY)))), 4), 0, 12632256, formatGmlText("Mods"));

        var tRows = _menuRowsFor('title');
        for (var tri = 0; tri < tRows.length; tri++) {
          draw_text_outlined(self, other, self.textX,
            yyfplus(yyfplus(real(self.textInitY), real(yyftime(5 + tri, real(self.textSpaceY)))), 4),
            0, 12632256, formatGmlText(tRows[tri].label));
        }
        if (yyfgreater(self.cursorIndex, 3.5)) {
          draw_set_color(16777215);
          draw_sprite(self, 25, 0, yyfminus(real(self.textX), 10),
            yyfplus(yyfplus(real(self.textInitY),
              real(yyftime(self.cursorIndex | 0, real(self.textSpaceY)))), 4));
        }
        var roomW0 = _Mb.room_width, totalRows0 = 5 + tRows.length;
        for (var ri = 0; ri < totalRows0; ri++) {
          var ry0 = yyfplus(yyfplus(real(self.textInitY), real(yyftime(ri, real(self.textSpaceY)))), 4);
          if (_mouseIn(self.textX - 12, ry0 - 7, 260, 16)) self.cursorIndex = ri;
          (function (idx) {
            _mouseZones.push({ x: self.textX - 12, y: ry0 - 7, w: 260, h: 16,
              action: function () {
                var c = null;
                try { c = resolveInstance(null, null, instance_find(33, 0)); } catch (e) { c = null; }
                self.cursorIndex = idx;
                if (c) c.pressed_Z = 1;
              } });
          })(ri);
        }
        var tTexts = _menuTextsFor('title');
        var tty = yyfplus(yyfplus(real(self.textInitY), real(yyftime(totalRows0, real(self.textSpaceY)))), 10);
        for (var tti = 0; tti < tTexts.length; tti++) {
          var ttx = tTexts[tti];
          if (ttx.kind === 'title') {
            draw_set_halign(1); draw_set_valign(1); draw_set_font(1); draw_set_color(12632256);
            draw_text(yyfdivide(real(roomW0), 2), yyfplus(tty, 6), ttx.text);
            draw_set_halign(0); draw_set_font(0); draw_set_valign(0);
            tty += 26;
          } else if (ttx.kind === 'subtitle') {
            draw_set_halign(1); draw_set_valign(1); draw_set_font(0); draw_set_color(12632256);
            draw_text(yyfdivide(real(roomW0), 2), yyfplus(tty, 4), ttx.text);
            draw_set_halign(0); draw_set_valign(0);
            tty += 14;
          } else {
            draw_text_outlined(self, other, self.textX, tty, 0, 8421504, formatGmlText(ttx.text));
            tty += 14;
          }
        }
        window.__modkitMenuDraw = false;
        return;
      }

      var roomW = _Mb.room_width, roomH = _Mb.room_height;
      draw_set_alpha(1);
      if (menuBackground) {

        var sx = roomW / menuBackground.w, sy = roomH / menuBackground.h;
        if (menuBackground.fit === 'cover') { sx = sy = Math.max(sx, sy); }
        draw_sprite_ext({ image_index: 0 }, menuBackground.index, 0,
          (roomW - menuBackground.w * sx) / 2, (roomH - menuBackground.h * sy) / 2,
          sx, sy, 0, 16777215, menuBackground.alpha === undefined ? 1 : menuBackground.alpha);
        if (menuBackground.dim > 0) {
          draw_set_color(0);
          draw_set_alpha(menuBackground.dim);
          draw_rectangle(0, 0, roomW, roomH, 0);
        }
      } else {
        draw_set_color(0);
        draw_rectangle(0, 0, roomW, roomH, 0);
      }
      draw_set_alpha(1);

      if (menuOpen) {
        // ---- the selected mod's own menu -------------------------------
        var modLine = self.modData ? self.modData[self.modSel] : null;
        draw_set_halign(1);
        draw_set_valign(1);
        draw_set_font(1);
        draw_set_color(12632256);
        draw_text(yyfdivide(real(roomW), 2), 40, modLine ? String(modLine.title) : 'Mod');
        draw_set_halign(0);
        draw_set_font(0);
        draw_set_valign(0);
        var rowYE = 80, rowHeightE = 20;
        if (!self.menuEntries || self.menuEntries.length === 0) {
          draw_set_valign(1);
          draw_text_outlined(self, other, yyfdivide(real(roomW), 2), 150, 0, 12632256,
            formatGmlText("This mod has no settings or commands."));
        }
        var paneXE = 205;
        draw_set_color(4210752);
        draw_line_width(195, 70, 195, yyfminus(real(roomH), 36), 2);
        var startRowE = Math.max(0, self.menuTop | 0);
        var selEnt = null;
        for (var ei = startRowE; self.menuEntries && ei < self.menuEntries.length; ei++) {
          if (yyfplus(real(rowYE), real(rowHeightE)) > yyfminus(real(roomH), 30)) break;
          var ent = self.menuEntries[ei];
          if (ei === self.menuSel) selEnt = ent;
          var line = entryLine(ent, modLine ? modLine.enabled : true);
          if (line.length > 31) line = line.slice(0, 30) + "\u2026";
          draw_text_outlined(self, other, 24, rowYE, 0, 12632256, formatGmlText(line));
          if (ei === self.menuSel) draw_sprite(self, 25, 0, 14, rowYE + 5);
          if (_mouseIn(14, rowYE - 1, _zw(line, 14, 178), 14)) self.menuSel = ei;
          (function (idx) {
            _mouseZones.push({ x: 14, y: rowYE - 1, w: _zw(line, 14, 178), h: 14,
              action: function () {
                var c = null;
                try { c = resolveInstance(null, null, instance_find(33, 0)); } catch (e) { c = null; }
                self.menuSel = idx;
                if (c) c.pressed_Z = 1;
              } });
          })(ei);
          rowYE += rowHeightE;
        }
        if (selEnt) {
          draw_set_color(12632256);
          draw_set_font(1);
          var detailTitle = (selEnt.kind === 'command') ? ('> ' + selEnt.name)
            : (selEnt.kind === 'setting') ? selEnt.entry.label : 'Mod enabled';
          draw_text_outlined(self, other, paneXE, 80, 0, 12632256,
            formatGmlText(String(detailTitle).slice(0, 22)));
          draw_set_font(0);
          var detail = '';
          if (selEnt.kind === 'enable')
            detail = 'Z flips this mod on or off. Reload the page to apply.';
          else if (selEnt.kind === 'setting') {
            if (selEnt.entry.type === 'toggle') detail = 'Z flips the value. Saved between sessions.';
            else if (selEnt.entry.type === 'number')
              detail = 'Value: ' + selEnt.entry.value + '   range ' + selEnt.entry.min + '..' + selEnt.entry.max +
                '   Z steps by ' + selEnt.entry.step;
            else detail = 'Z runs it now.';
          } else detail = selEnt.desc || '';
          if (detail.length > 260) detail = detail.slice(0, 259) + "\u2026";
          draw_text_ext(paneXE, 108, detail, rowHeightE, yyfminus(real(roomW), paneXE + 15));
        }
        draw_set_halign(1);
        draw_set_valign(2);
        draw_set_font(0);
        draw_set_color(12632256);
        draw_text_outlined(self, other, yyfdivide(real(roomW), 2), yyfminus(real(roomH), 3), 4210752, 12632256,
          self.modToggled >= 0 ? "Reload the page to apply changes  -  Z: use  X: back" : "Z: use  X: back");
        draw_set_color(16777215);
        window.__modkitMenuDraw = false;
        return;
      }

      // ---- installed-mods list (one line per mod) -----------------------
      draw_set_halign(1);
      draw_set_valign(1);
      draw_set_font(1);
      draw_set_color(12632256);
      draw_text(yyfdivide(real(roomW), 2), 40, "Installed Mods");
      draw_set_halign(0);
      draw_set_font(0);
      draw_set_valign(0);
      var rowY = 80, rowHeight = 20;
      if (!self.modData || self.modData.length === 0) {
        draw_set_valign(1);
        draw_text_outlined(self, other, yyfdivide(real(roomW), 2), 150, 0, 12632256,
          formatGmlText("No mods installed."));
        draw_text_outlined(self, other, yyfdivide(real(roomW), 2), 170, 0, 12632256,
          formatGmlText("Pack zips into the mods/ folder and rebuild."));
      }

      var paneX = 205;
      draw_set_color(4210752);
      draw_line_width(195, 70, 195, yyfminus(real(roomH), 36), 2);
      var startRow = Math.max(0, self.modTop | 0);
      var selInfo = null;
      for (var i = startRow; self.modData && i < self.modData.length; i++) {
        if (yyfplus(real(rowY), real(rowHeight)) > yyfminus(real(roomH), 30)) break;
        var modInfo = self.modData[i];
        if (i === self.modSel) selInfo = modInfo;
        var line = (modInfo.failed ? '! ' : '') + modInfo.title;

        if (line.length > 31) line = line.slice(0, 30) + "\u2026";
        draw_text_outlined(self, other, 24, rowY, 0, modInfo.failed ? 255 : (modInfo.enabled ? 12632256 : 8421504), formatGmlText(line));

        if (i === self.modSel) draw_sprite(self, 25, 0, 14, rowY + 5);
        if (_mouseIn(14, rowY - 1, _zw(line, 14, 178), 14)) self.modSel = i;
        (function (idx) {
          _mouseZones.push({ x: 14, y: rowY - 1, w: _zw(line, 14, 178), h: 14,
            action: function () {
              var c = null;
              try { c = resolveInstance(null, null, instance_find(33, 0)); } catch (e) { c = null; }
              self.modSel = idx;
              if (c) c.pressed_Z = 1;
            } });
        })(i);
        rowY += rowHeight;
      }

      if (selInfo) {
        var paneW = yyfminus(real(roomW), paneX + 15);
        draw_set_color(12632256);
        draw_set_font(1);
        var icoT = (selInfo.icon >= 0) ? selInfo.icon : -1;
        if (icoT >= 0) draw_sprite(self, icoT, 0, yyfminus(real(roomW), 15 + sprite_get_width(icoT)), 72);
        draw_text_outlined(self, other, paneX, 80, 0, 12632256, formatGmlText(String(selInfo.title).slice(0, icoT >= 0 ? 16 : 22)));
        draw_set_font(0);
        var meta = "v" + selInfo.version + (selInfo.author ? "  by " + selInfo.author : "");
        draw_text_outlined(self, other, paneX, 108, 0, 8421504, formatGmlText(meta));
        draw_text_outlined(self, other, paneX, 126, 0, selInfo.failed ? 255 : (selInfo.enabled ? 12632256 : 8421504),
          formatGmlText(selInfo.failed ? "FAILED TO LOAD" : (selInfo.enabled ? "ENABLED" : "DISABLED")));
        draw_text_outlined(self, other, paneX, 144, 0, 8421504, formatGmlText("Z: settings & commands"));
        draw_set_color(4210752);
        var desc = selInfo.failed ? String(selInfo.failed) : (selInfo.description ? String(selInfo.description) : "");
        if (!desc.length) desc = "No description.";

        if (desc.length > 260) desc = desc.slice(0, 259) + "\u2026";
        draw_text_ext(paneX, 166, desc, rowHeight, paneW);
      }
      draw_set_halign(1);
      draw_set_valign(2);
      draw_set_font(0);
      draw_set_color(12632256);
      draw_text_outlined(self, other, yyfdivide(real(roomW), 2), yyfminus(real(roomH), 3), 4210752, 12632256,
        self.modToggled >= 0 ? "Reload the page to apply changes  -  Z: open  X: back" : "Z: open  X: back");
      draw_set_color(16777215);
      window.__modkitMenuDraw = false;
    };
    _menuShared = {
      modListData: modListData, modMenuEntries: modMenuEntries, entryLine: entryLine
    };
    log('[modkit] title-screen "Mods" menu installed');
  }


  // ===== PAUSE MENU ====================================================
  // Replaces the stock obj_Pause black screen ("Paused" + a bottom note)
  // with a real menu: PAUSED title, Continue / Settings / Mods rows.
  // The Mods row opens the same installed-mods list + per-mod submenu as
  // the title screen. Game settings persist in localStorage
  // ('modkit_gamesettings'): audio master volume (audio_master_gain) and the
  // pet firefly count (only offered when the FireflyPet mod is loaded).
  var _menuShared = null;

  function installPauseMenu() {
    if (!_menuShared) { log('[modkit] pause menu not installed (no mod menu helpers)'); return; }
    var pauseIdx = findIdx(gamedata[GD.objects], 'obj_Pause');
    if (pauseIdx < 0) { log('[modkit] obj_Pause not found; pause menu not installed'); return; }
    var entry = gamedata[GD.objects][pauseIdx];

    var st = { screen: 'main', sel: 0, setSel: 0,
               listOpen: 0, listSel: 0, listTop: 0, listData: null, modToggled: -1,
               menuOpen: 0, menuEntries: null, menuSel: 0, menuTop: 0 };

    // ---- game settings (persisted in localStorage) ---------------------
    var gameSet = { volume: 10 };
    try {
      var raw = window.localStorage.getItem('modkit_gamesettings');
      if (raw) {
        var gs = JSON.parse(raw);
        if (gs && isFinite(gs.volume)) gameSet.volume = Math.max(0, Math.min(10, gs.volume | 0));
      }
    } catch (e) { }
    var _volApplied = -1;
    function saveGameSet() {
      try { window.localStorage.setItem('modkit_gamesettings', JSON.stringify(gameSet)); } catch (e) { }
    }
    function applyVolume() {
      if (_volApplied === gameSet.volume) return;
      try { audio_master_gain(gameSet.volume / 10); _volApplied = gameSet.volume; }
      catch (e) { _volApplied = -1; }
    }
    function settingRows() {
      // firefly count lives in the FireflyPet mod's own settings now
      return [{ id: 'volume', label: 'Volume', value: gameSet.volume, max: 10 }];
    }

    st.gameSet = gameSet;
    if (window.ModKit) window.ModKit._pauseState = st;

      function continueGame(self) {
        var pl = null;
        try { pl = resolveInstance(null, null, (self && self.player !== undefined) ? self.player : instance_find(36, 0)); }
        catch (e) { pl = null; }
        if (pl) pl.active = 1;
        if (self) { try { instance_destroy(self); } catch (e) {} }
      }

      // "Quit": exactly what the stock Q key does (the stock Step runs the
      // handler cleanup + room_goto(title)); guarded so a double press can't
      // fire two room changes mid-fade
      function quitToTitle() {
        if (st.quitting || !pauseSelf) return;
        st.quitting = true;
        var qc = null;
        try { qc = resolveInstance(null, null, (pauseSelf.con !== undefined) ? pauseSelf.con : instance_find(33, 0)); }
        catch (e) { qc = null; }
        if (qc) qc.pressed_Q = 1;
        audio_play_sound(17, 2, 0);
        if (origStep) origStep(pauseSelf, pauseSelf);
      }

      function runPauseRow(modRowsL, i) {
        if (i === 0) { continueGame(pauseSelf); return; }
        if (i === 1) { st.screen = 'settings'; st.setSel = 0; audio_play_sound(17, 2, 0); return; }
        if (i === 2) {
          st.listOpen = 1; st.listData = _menuShared.modListData();
          st.listSel = 0; st.listTop = 0;
          audio_play_sound(17, 2, 0);
          return;
        }
        if (i === 3 + modRowsL.length) { quitToTitle(); return; }
        var rec = modRowsL[i - 3];
        if (rec) { try { rec.onSelect(); audio_play_sound(17, 2, 0); }
          catch (err) { loud('pause menu row "' + rec.label + '" failed', err); } }
      }

      function activatePauseEntry(ent) {
        {
          if (ent.kind === 'enable') {
            var modInfo = st.listData[st.listSel];
            var dl = getDisabled();
            var fi = dl.indexOf(modInfo.name);
            if (fi >= 0) dl.splice(fi, 1); else dl.push(modInfo.name);
            setDisabled(dl);
            modInfo.enabled = !modInfo.enabled;
            st.modToggled = 1;
            audio_play_sound(17, 2, 0);
          } else if (ent.kind === 'setting') {
            if (ent.entry.type === 'toggle') _settingToggle(ent.entry);
            else if (ent.entry.type === 'number') _settingAdjust(ent.entry, 1);
            else _settingRun(ent.entry);
            audio_play_sound(17, 2, 0);
          } else if (ent.kind === 'command' && ent.fn) {
            try { ent.fn(); audio_play_sound(17, 2, 0); }
            catch (err) { loud('pause menu command "' + ent.name + '" failed', err); }
          }
        }
      }
    var pauseSelf = null;
    var origStep = entry.ev_Step || null;
    entry.ev_Step = function (self, other) {
      if (pauseSelf !== self) st.quitting = false;
      pauseSelf = self;
      var con = null;
      try { con = resolveInstance(self, other, (self.con !== undefined) ? self.con : instance_find(33, 0)); }
      catch (e) { con = null; }
      applyVolume();
      function eat(k) {
        if (!con) return false;
        var v = yyGetBool(con[k]);
        if (v) con[k] = 0;
        return v;
      }
      // ---- per-mod menu (same entries as the title screen) --------------
      if (st.menuOpen) {
        var entCount = st.menuEntries ? st.menuEntries.length : 0;
        if (eat('pressed_up')) { st.menuSel--; audio_play_sound(16, 1, 0); }
        if (eat('pressed_down')) { st.menuSel++; audio_play_sound(16, 1, 0); }
        if (entCount > 0) {
          if (st.menuSel > entCount - 1) st.menuSel = 0;
          if (st.menuSel < 0) st.menuSel = entCount - 1;
          var pageM = Math.max(1, Math.floor((gml_Script_hview(-4, -4) - 150) / 20));
          if (st.menuSel < st.menuTop) st.menuTop = st.menuSel;
          if (st.menuSel > st.menuTop + pageM - 1) st.menuTop = st.menuSel - pageM + 1;
        } else { st.menuSel = 0; st.menuTop = 0; }
        if ((eat('pressed_Z') || eat('pressed_space')) && entCount > 0) {
          activatePauseEntry(st.menuEntries[st.menuSel]);
        }
        if (eat('pressed_X')) st.menuOpen = 0;
        return;
      }
      // ---- installed-mods list -----------------------------------------
      if (st.listOpen) {
        var modCount = st.listData ? st.listData.length : 0;
        if (eat('pressed_up')) { st.listSel--; audio_play_sound(16, 1, 0); }
        if (eat('pressed_down')) { st.listSel++; audio_play_sound(16, 1, 0); }
        if (modCount > 0) {
          if (st.listSel > modCount - 1) st.listSel = 0;
          if (st.listSel < 0) st.listSel = modCount - 1;
          var page = Math.max(1, Math.floor((gml_Script_hview(-4, -4) - 150) / 20));
          if (st.listSel < st.listTop) st.listTop = st.listSel;
          if (st.listSel > st.listTop + page - 1) st.listTop = st.listSel - page + 1;
        } else { st.listSel = 0; st.listTop = 0; }
        if ((eat('pressed_Z') || eat('pressed_space')) && modCount > 0) {
          st.menuEntries = _menuShared.modMenuEntries(st.listData[st.listSel].name);
          st.menuOpen = 1; st.menuSel = 0; st.menuTop = 0;
          audio_play_sound(17, 2, 0);
        }
        if (eat('pressed_X')) st.listOpen = 0;
        return;
      }

      // ---- settings screen ---------------------------------------------
      if (st.screen === 'settings') {
        var rows = settingRows(), n = rows.length;
        if (eat('pressed_up')) { st.setSel = (st.setSel - 1 + n) % n; audio_play_sound(16, 1, 0); }
        if (eat('pressed_down')) { st.setSel = (st.setSel + 1) % n; audio_play_sound(16, 1, 0); }
        if (eat('pressed_Z') || eat('pressed_space')) {
          var row = rows[st.setSel];
          row.value = (row.value + 1) % (row.max + 1);
          if (row.id === 'volume') { gameSet.volume = row.value; applyVolume(); }
          saveGameSet();
          audio_play_sound(17, 2, 0);
        }
        if (eat('pressed_X') || eat('pressed_P')) st.screen = 'main';
        return;
      }

      // ---- main screen (stock rows + mod rows) ---------------------------
      var modRowsL = _menuRowsFor('pause');
      var mainCount = 3 + modRowsL.length + 1;   // + Quit
      if (eat('pressed_up')) { st.sel = (st.sel - 1 + mainCount) % mainCount; audio_play_sound(16, 1, 0); }
      if (eat('pressed_down')) { st.sel = (st.sel + 1) % mainCount; audio_play_sound(16, 1, 0); }
      if (eat('pressed_Z') || eat('pressed_space')) { runPauseRow(modRowsL, st.sel); return; }
      if (eat('pressed_P')) { continueGame(self); return; }
      // Q = quit to title: let the stock Step handle it
      if (origStep) origStep(self, other);
    };

    var origDraw = entry.ev_Draw || null;
    entry.ev_Draw = function (self, other) {
      window.__modkitMenuDraw = true;
      var vx = gml_Script_xview(-4, -4), vy = gml_Script_yview(-4, -4);
      var vw = gml_Script_wview(-4, -4), vh = gml_Script_hview(-4, -4);
      applyVolume();
      draw_set_alpha(0.88);
      draw_set_color(0);
      draw_rectangle(vx, vy, vx + vw, vy + vh, 0);
      draw_set_alpha(1);
      function rowLine(x, y, line, color) {
        if (line.length > 31) line = line.slice(0, 30) + "‥";
        draw_text_outlined(self, other, x, y, 0, color, formatGmlText(line));
      }
      function bottomHint(text) {
        draw_set_halign(1); draw_set_valign(2); draw_set_font(0); draw_set_color(12632256);
        draw_text_outlined(self, other, vx + vw / 2, vy + vh - 3, 4210752, 12632256, formatGmlText(text));
      }

      if (st.menuOpen) {
        var modLine = st.listData ? st.listData[st.listSel] : null;
        draw_set_halign(1); draw_set_valign(1); draw_set_font(1); draw_set_color(12632256);
        draw_text(vx + vw / 2, vy + 40, modLine ? String(modLine.title) : 'Mod');
        draw_set_halign(0); draw_set_font(0); draw_set_valign(0);
        draw_set_color(4210752);
        draw_line_width(vx + 195, vy + 70, vx + 195, vy + vh - 36, 2);
        var rowYE = vy + 80, rowHeightE = 20, selEnt = null;
        for (var ei = st.menuTop | 0; st.menuEntries && ei < st.menuEntries.length; ei++) {
          if (rowYE + rowHeightE > vy + vh - 30) break;
          var ent = st.menuEntries[ei];
          if (ei === st.menuSel) selEnt = ent;
          rowLine(vx + 24, rowYE, _menuShared.entryLine(ent, modLine ? modLine.enabled : true),
                  modLine && !modLine.enabled && ent.kind === 'enable' ? 8421504 : 12632256);
          if (ei === st.menuSel) draw_sprite(self, 25, 0, vx + 14, rowYE + 5);
          var entTxt = _menuShared.entryLine(ent, modLine ? modLine.enabled : true);
          if (_mouseIn(vx + 14, rowYE - 1, _zw(entTxt, 14, 178), 14)) st.menuSel = ei;
          (function (zent, idx) {
            _mouseZones.push({ x: vx + 14, y: rowYE - 1, w: _zw(entTxt, 14, 178), h: 14,
              action: function () { st.menuSel = idx; activatePauseEntry(zent); } });
          })(ent, ei);
          rowYE += rowHeightE;
        }
        if (selEnt) {
          draw_set_color(12632256); draw_set_font(1);
          var dTitle = (selEnt.kind === 'command') ? ('> ' + selEnt.name)
            : (selEnt.kind === 'setting') ? selEnt.entry.label : 'Mod enabled';
          rowLine(vx + 205, vy + 80, String(dTitle).slice(0, 22), 12632256);
          draw_set_font(0);
          var detail = '';
          if (selEnt.kind === 'enable') detail = 'Z flips this mod on or off. Reload the page to apply.';
          else if (selEnt.kind === 'setting') {
            if (selEnt.entry.type === 'toggle') detail = 'Z flips the value. Saved between sessions.';
            else if (selEnt.entry.type === 'number')
              detail = 'Value: ' + selEnt.entry.value + '   range ' + selEnt.entry.min + '..' + selEnt.entry.max + '   Z steps by ' + selEnt.entry.step;
            else detail = 'Z runs it now.';
          } else detail = selEnt.desc || '';
          if (detail.length > 260) detail = detail.slice(0, 259) + "‥";
          draw_text_ext(vx + 205, vy + 108, detail, rowHeightE, vw - 205 - 15);
        }
        bottomHint('Z: use  X: back');
      } else if (st.listOpen) {
        draw_set_halign(1); draw_set_valign(1); draw_set_font(1); draw_set_color(12632256);
        draw_text(vx + vw / 2, vy + 40, 'Installed Mods');
        draw_set_halign(0); draw_set_font(0); draw_set_valign(0);
        draw_set_color(4210752);
        draw_line_width(vx + 195, vy + 70, vx + 195, vy + vh - 36, 2);
        var rowY = vy + 80, rowHeight = 20, selInfo = null;
        for (var i = st.listTop | 0; st.listData && i < st.listData.length; i++) {
          if (rowY + rowHeight > vy + vh - 30) break;
          var modInfo = st.listData[i];
          if (i === st.listSel) selInfo = modInfo;
          rowLine(vx + 24, rowY, (modInfo.failed ? '! ' : '') + modInfo.title, modInfo.failed ? 255 : (modInfo.enabled ? 12632256 : 8421504));
          if (i === st.listSel) draw_sprite(self, 25, 0, vx + 14, rowY + 5);
          if (_mouseIn(vx + 14, rowY - 1, _zw(modInfo.title, 14, 178), 14)) st.listSel = i;
          (function (idx) {
            _mouseZones.push({ x: vx + 14, y: rowY - 1, w: _zw(modInfo.title, 14, 178), h: 14,
              action: function () {
                st.listSel = idx;
                st.menuEntries = _menuShared.modMenuEntries(st.listData[idx].name);
                st.menuOpen = 1; st.menuSel = 0; st.menuTop = 0;
                audio_play_sound(17, 2, 0);
              } });
          })(i);
          rowY += rowHeight;
        }
        if (selInfo) {
          draw_set_color(12632256); draw_set_font(1);
          var icoP = (selInfo.icon >= 0) ? selInfo.icon : -1;
          if (icoP >= 0) draw_sprite(self, icoP, 0, vx + vw - 15 - sprite_get_width(icoP), vy + 72);
          rowLine(vx + 205, vy + 80, String(selInfo.title).slice(0, icoP >= 0 ? 16 : 22), 12632256);
          draw_set_font(0);
          rowLine(vx + 205, vy + 108, 'v' + selInfo.version + (selInfo.author ? '  by ' + selInfo.author : ''), 8421504);
          rowLine(vx + 205, vy + 126, selInfo.failed ? 'FAILED TO LOAD' : (selInfo.enabled ? 'ENABLED' : 'DISABLED'),
                  selInfo.failed ? 255 : (selInfo.enabled ? 12632256 : 8421504));
          rowLine(vx + 205, vy + 144, 'Z: settings & commands', 8421504);
          draw_set_color(4210752);
          var desc = selInfo.failed ? String(selInfo.failed) : (selInfo.description ? String(selInfo.description) : 'No description.');
          if (desc.length > 260) desc = desc.slice(0, 259) + "‥";
          draw_text_ext(vx + 205, vy + 166, desc, rowHeight, vw - 205 - 15);
        }
        bottomHint(st.modToggled ? 'Reload the page to apply changes  -  Z: open  X: back' : 'Z: open  X: back');
      } else if (st.screen === 'settings') {
        draw_set_halign(1); draw_set_valign(1); draw_set_font(1); draw_set_color(12632256);
        draw_text(vx + vw / 2, vy + 40, 'Settings');
        draw_set_halign(0); draw_set_font(0); draw_set_valign(0);
        var rows = settingRows(), rowY2 = vy + 108;
        for (var r = 0; r < rows.length; r++) {
          var txt = rows[r].label + ': ' + rows[r].value + (rows[r].id === 'volume' ? ' / ' + rows[r].max : '');
          rowLine(vx + 148, rowY2, txt, 12632256);
          if (r === st.setSel) draw_sprite(self, 25, 0, vx + 138, rowY2 + 5);
          if (_mouseIn(vx + 138, rowY2 - 1, _zw(txt, 14, 170), 14)) st.setSel = r;
          (function (row, idx) {
            _mouseZones.push({ x: vx + 138, y: rowY2 - 1, w: _zw(txt, 14, 170), h: 14,
              action: function () {
                st.setSel = idx;
                row.value = (row.value + 1) % (row.max + 1);
                if (row.id === 'volume') { gameSet.volume = row.value; applyVolume(); saveGameSet(); }
                audio_play_sound(17, 2, 0);
              } });
          })(rows[r], r);
          rowY2 += 20;
        }
        bottomHint('Z: adjust  X: back');
      } else {
        draw_set_halign(1); draw_set_valign(1); draw_set_font(1); draw_set_color(12632256);
        draw_text(vx + vw / 2, vy + 64, 'PAUSED');
        draw_set_halign(0); draw_set_font(0); draw_set_valign(0);
        var modRowsL = _menuRowsFor('pause');
        var mainLabels = ['Continue', 'Settings', 'Mods'];
        for (var mr = 0; mr < modRowsL.length; mr++) mainLabels.push(modRowsL[mr].label);
        mainLabels.push('Quit');
        var modRowsStepL = modRowsL;
        var rowY3 = vy + 108;
        for (var m = 0; m < mainLabels.length; m++) {
          rowLine(vx + 148, rowY3, mainLabels[m], 12632256);
          if (m === st.sel) draw_sprite(self, 25, 0, vx + 138, rowY3 + 5);
          if (_mouseIn(vx + 138, rowY3 - 1, _zw(mainLabels[m], 14, 170), 14)) st.sel = m;
          (function (idx) {
            _mouseZones.push({ x: vx + 138, y: rowY3 - 1, w: _zw(mainLabels[m], 14, 170), h: 14,
              action: function () { st.sel = idx; runPauseRow(modRowsStepL, idx); } });
          })(m);
          rowY3 += 20;
        }
        var modTextsL = _menuTextsFor('pause');
        var ty = rowY3 + 10;
        for (var mtI = 0; mtI < modTextsL.length; mtI++) {
          var mt = modTextsL[mtI];
          if (mt.kind === 'title') {
            draw_set_halign(1); draw_set_valign(1); draw_set_font(1); draw_set_color(12632256);
            draw_text(vx + vw / 2, ty + 8, mt.text);
            draw_set_halign(0); draw_set_font(0); draw_set_valign(0);
            ty += 26;
          } else if (mt.kind === 'subtitle') {
            draw_set_halign(1); draw_set_valign(1); draw_set_font(0); draw_set_color(12632256);
            draw_text(vx + vw / 2, ty + 5, mt.text);
            draw_set_halign(0); draw_set_valign(0);
            ty += 14;
          } else {
            rowLine(vx + 148, ty, mt.text, 8421504);
            ty += 14;
          }
        }
        bottomHint('Z or click: select  X/P: back  Q: quit');
      }
      draw_set_color(16777215);
      window.__modkitMenuDraw = false;
    };
    log('[modkit] pause menu installed');
  }


  // ---- in-game HUD hint zones ("SPACE: Inventory", "X/K: Clues", "C/L ->",
  //      "SPACE: Close", "Z /J: Use item") ---------------------------------------
  // Extra inventory slots for ModKit.inventory.add items: the bar stays 4 slots wide and scrolls horizontally,
  // with a thin scrollbar along the bottom edge of the box. Without registered items nothing here runs.
  function installInventorySlots() {
    var oi = findIdx(gamedata[GD.objects], 'obj_Inventory');
    if (oi < 0) return;
    var ie = gamedata[GD.objects][oi];
    var VIS = 4, STOCK = 4;
    var origCreate = ie.ev_Create, origBegin = ie.ev_BeginStep, origDraw = ie.ev_Draw;
    ie.ev_Create = function (self, other) {
      if (origCreate) origCreate(self, other);
      if (_invItems.length) { self.itemsMax = STOCK + _invItems.length - 1; self.mkScroll = 0; }
    };
    ie.ev_BeginStep = function (self, other) {
      // Using an item closes the bar: a custom item with an onUse (or any dialog it opened), or a stock item
      // whose use flag the stock BeginStep raises on the player. The one-frame flag is still seen by the
      // objects it targets this frame; the stock CleanUp unfreezes the player.
      var used = false;
      try {
        var c = resolveInstance(self, other, self.con);
        if (yyGetBool(c.pressed_Z) && _invItems.length && self.itemIndex >= STOCK) {
          var it = _invItems[self.itemIndex - STOCK];
          if (it && _invOwned(it.id) && it.onUse) {
            try { it.onUse({ id: it.id, name: it.name, slot: self.itemIndex }); }
            catch (err) { loud('inventory onUse failed (' + it.id + ')', err); }
            used = true;
          }
        }
      } catch (e) { }
      if (!used && origBegin) {
        origBegin(self, other);
        try {
          var pl = resolveInstance(self, other, self.player);
          used = !!(yyGetBool(pl.roping) || yyGetBool(pl.plungering) || yyGetBool(pl.shoveling) || yyGetBool(pl.lightering));
        } catch (e2) { }
      }
      if (used) {
        try { resolveInstance(self, other, self.con).pressed_Z = 0; } catch (e3) { }
        instance_destroy(self);
      }
    };
    ie.ev_Draw = function (self, other) {
      if (!_invItems.length) { if (origDraw) origDraw(self, other); return; }
      try { drawExtendedBar(self, other); }
      catch (err) { loud('inventory bar draw failed (falling back to the stock bar)', err); if (origDraw) origDraw(self, other); }
    };
    function drawExtendedBar(self, other) {
      var n = STOCK + _invItems.length, w = self.recWidth;
      var xv = gml_Script_xview(self, other), yv = gml_Script_yview(self, other), hv = gml_Script_hview(self, other);
      var x0 = xv + 4, bot = yv + hv - 5, top = bot - w;
      var sc = self.mkScroll | 0;
      if (self.itemIndex < sc) sc = self.itemIndex;
      if (self.itemIndex > sc + VIS - 1) sc = self.itemIndex - (VIS - 1);
      sc = Math.max(0, Math.min(n - VIS, sc));
      self.mkScroll = sc;
      draw_set_color(0); draw_set_alpha(0.4);
      draw_rectangle(x0, top, x0 + VIS * w, bot, 0);
      draw_set_color(8421504); draw_set_alpha(1);
      for (var i = 0; i < VIS; i++) draw_rect_width(self, other, x0 + i * w, top, x0 + w + i * w, bot, 1);
      draw_set_color(12632256);
      draw_rect_width(self, other, x0 + (self.itemIndex - sc) * w, top, x0 + w + (self.itemIndex - sc) * w, bot, 2);
      draw_set_color(8421504); draw_set_halign(0); draw_set_valign(2); draw_set_font(0);
      draw_text(x0, top - 1, 'SPACE: Close - Z /J: Use item');
      draw_set_color(16777215);
      var stock = [[self.rope, 36], [self.plunger, 4], [self.shovel, 83], [self.lighter, 78]];
      var names = ['Rope', 'Plunger', 'Shovel', 'Lighter'];
      var label = '';
      for (var k = sc; k < sc + VIS; k++) {
        var cx = x0 + (k - sc) * w + w / 2, cy = bot - w / 2, owned, spr, fr = 0, nm;
        if (k < STOCK) { owned = yyGetBool(stock[k][0]); spr = stock[k][1]; nm = names[k]; }
        else { var it = _invItems[k - STOCK]; owned = _invOwned(it.id); spr = it.image; fr = it.frame; nm = it.name; }
        if (owned) draw_sprite(self, spr, fr, cx, cy);
        if (owned && k === self.itemIndex) label = nm;
        var kk = k;
        _mouseZones.push({ x: x0 + (k - sc) * w, y: top, w: w, h: w - 4, action: function () {
          var c2 = null; try { c2 = resolveInstance(null, null, instance_find(33, 0)); } catch (e2) { }
          if (kk === self.itemIndex) { if (c2) c2.pressed_Z = 1; } else { self.itemIndex = kk; audio_play_sound(22, 1, 0); }
        } });
      }
      draw_set_color(8421504); draw_set_valign(1);
      var desc = '';
      if (self.itemIndex >= STOCK && label) desc = _invItems[self.itemIndex - STOCK].description || '';
      if (desc.length > 40) desc = desc.slice(0, 39) + '\u2026';
      draw_text(xv + 7 + VIS * w, bot - w / 2 - 1 - (desc ? 6 : 0), label);
      if (desc) { draw_set_color(6316128); draw_text(xv + 7 + VIS * w, bot - w / 2 + 7, desc); }
      // horizontal scrollbar along the bottom edge of the box
      var trackW = VIS * w, thumbW = Math.max(8, trackW * VIS / n);
      var thumbX = x0 + (trackW - thumbW) * (n > VIS ? sc / (n - VIS) : 0);
      draw_set_color(4210752); draw_set_alpha(0.8);
      draw_rectangle(x0 + 1, bot - 4, x0 + trackW - 1, bot - 1, 0);
      draw_set_color(12632256); draw_set_alpha(1);
      draw_rectangle(thumbX + 1, bot - 4, thumbX + thumbW - 1, bot - 1, 0);
      draw_set_color(16777215);
      _mouseZones.push({ x: x0, y: bot - 5, w: trackW, h: 5, action: function () {
        var c3 = null; try { c3 = resolveInstance(null, null, instance_find(33, 0)); } catch (e3) { }
        if (!c3) return;
        if (mouse_x(0) < x0 + trackW / 2) c3.pressed_left = 1; else c3.pressed_right = 1;
      } });
    }
  }

  function installHudHints() {
    var hudIdx = findIdx(gamedata[GD.objects], 'obj_HUD');
    if (hudIdx < 0) return;
    var entry = gamedata[GD.objects][hudIdx];
    // While the pause menu is up, everything the player can see or operate
    // in-game must stand still. Stock pause only deactivates the player, so
    // objects that read the shared controller keys (X/K clues, SPACE inventory,
    // dialog boxes, puzzles) would react to the keys the pause menu uses.
    ['obj_HUD', 'obj_Inventory', 'obj_TextBox', 'obj_Abyss', 'obj_InvisMaze'].forEach(function (nm) {
      var oi = findIdx(gamedata[GD.objects], nm);
      if (oi < 0) return;
      var oe = gamedata[GD.objects][oi];
      var os = oe.ev_Step;
      if (!os) return;
      oe.ev_Step = function (self, other) {
        if (yyGetBool(instance_exists(49))) return;
        return os(self, other);
      };
    });
    var origDraw = entry.ev_Draw || null;
    entry.ev_Draw = function (self, other) {
      if (origDraw) origDraw(self, other);
      try { _toastDraw(); } catch (e) { }
      try {
        // "P: Pause" bottom-right, styled like the other overlay hints
        if (!yyGetBool(instance_exists(28)) && !yyGetBool(instance_exists(67)) && !yyGetBool(instance_exists(49)) &&
            !(dialog && !dialog.stock)) {
          var pvx = gml_Script_xview(-4, -4), pvy = gml_Script_yview(-4, -4);
          var pvw = gml_Script_wview(-4, -4), pvh = gml_Script_hview(-4, -4);
          draw_set_color(8421504); draw_set_halign(2); draw_set_valign(2); draw_set_font(0);
          draw_text(pvx + pvw - 2, pvy + pvh, 'P: Pause');
          draw_set_halign(0); draw_set_color(16777215);
          var pcon = null;
          try { pcon = resolveInstance(null, null, instance_find(33, 0)); } catch (e) { pcon = null; }
          if (pcon) _mouseZones.push({ x: pvx + pvw - 2 - 58, y: pvy + pvh - 12, w: 58, h: 12,
            action: function () { pcon.pressed_P = 1; } });
        }
        var con = null;
        try { con = resolveInstance(null, null, instance_find(33, 0)); } catch (e) { con = null; }
        if (!con || yyGetBool(instance_exists(49))) return;
        var vx = gml_Script_xview(-4, -4), vy = gml_Script_yview(-4, -4);
        var vh = gml_Script_hview(-4, -4);
        // zone lives even while the inventory is open: clicking the hint
        // toggles it closed again (the stock hint only DRAWS when closed)
        if (!yyGetBool(instance_exists(67)))
          _mouseZones.push({ x: vx + 2, y: vy + vh - 12, w: 125, h: 12,
            action: function () { con.pressed_space = 1; } });
        if (typeof self.clueNumber === 'number' && self.clueNumber > 0.5)
          _mouseZones.push({ x: vx + 2, y: vy, w: 90, h: 12,
            action: function () { con.pressed_X = 1; } });
        // "C/L ->" beside the open clue scroll pages to the next clue
        if (self.toggled && typeof self.clueNumber === 'number' && self.clueNumber > 1.5)
          _mouseZones.push({ x: self.x + self.clueWidth2 + 1, y: self.y - 7, w: 46, h: 14,
            action: function () { con.pressed_C = 1; } });
        // inventory hint line "SPACE: Close - Z /J: Use item" (text starts at view x + 4,
        // bottom-aligned just above the slot row; ~7 px per glyph in font 0)
        if (yyGetBool(instance_exists(28))) {
          var inv = resolveInstance(null, null, instance_find(28, 0));
          var rw = (inv && typeof inv.recWidth === 'number') ? inv.recWidth : 30;
          var invY = vy + vh - 6 - rw - 12;
          _mouseZones.push({ x: vx + 2, y: invY, w: 92, h: 14,
            action: function () { con.pressed_space = 1; } });
          _mouseZones.push({ x: vx + 4 + 100, y: invY, w: 104, h: 14,
            action: function () { con.pressed_Z = 1; } });
        }
      } catch (e) { /* hud hints are best-effort */ }
    };
    // the stock text box's "Z / J" hint and the player's interact bubble are clickable too
    function conOf() {
      var c = null;
      try { c = resolveInstance(null, null, instance_find(33, 0)); } catch (e) { c = null; }
      return c;
    }
    var tbIdx = findIdx(gamedata[GD.objects], 'obj_TextBox');
    if (tbIdx >= 0) {
      var tbEntry = gamedata[GD.objects][tbIdx], tbDraw = tbEntry.ev_Draw || null;
      tbEntry.ev_Draw = function (self, other) {
        if (tbDraw) tbDraw(self, other);
        try {
          if (yyGetBool(instance_exists(49))) return;
          _mouseZones.push({ x: self.x + 122, y: self.y - 19, w: 42, h: 13,
            action: function () { var c = conOf(); if (c) c.pressed_Z = 1; } });
        } catch (e) { }
      };
    }
    var plIdx = findIdx(gamedata[GD.objects], 'obj_Player');
    if (plIdx >= 0) {
      var plEntry = gamedata[GD.objects][plIdx], plDraw = plEntry.ev_Draw || null;
      plEntry.ev_Draw = function (self, other) {
        if (plDraw) plDraw(self, other);
        try {
          if (self.interactable === -4 || yyGetBool(instance_exists(49)) || yyGetBool(instance_exists(67))) return;
          // the object the player is facing (the same probe that shows the Z/J bubble) becomes clickable;
          // the bubble itself is not a click target
          if (!yyGetBool(instance_exists(self.interactable))) return;
          var tgt = resolveInstance(self, other, self.interactable);
          if (!tgt) return;
          var bl = tgt.bbox_left, br = tgt.bbox_right, bt = tgt.bbox_top, bb = tgt.bbox_bottom;
          if (typeof bl !== 'number' || typeof br !== 'number' || isNaN(bl + br + bt + bb)) {
            bl = tgt.x - 12; br = tgt.x + 12; bt = tgt.y - 12; bb = tgt.y + 12;
          }
          _mouseZones.push({ x: bl, y: bt, w: Math.max(8, br - bl), h: Math.max(8, bb - bt),
            action: function () { var c = conOf(); if (c) c.pressed_Z = 1; } });
        } catch (e) { }
      };
    }
    installInventorySlots();
    log('[modkit] HUD hint mouse zones installed');
  }



  function resolveURL(url) {
    if (!url) return url;
    var redirectEntry = redirectMap[url];
    if (!redirectEntry) {
      var base = url.indexOf('/') >= 0 ? url.slice(url.lastIndexOf('/') + 1) : url;
      redirectEntry = redirectMap['html5game/' + base];
    }
    if (!redirectEntry) return url;
    if (redirectEntry.url) return redirectEntry.url;
    for (var i = 0; i < allMods.length; i++) {
      if (allMods[i].name === redirectEntry.mod && allMods[i].__fileURL) {

        try {
          var served = allMods[i].__fileURL(redirectEntry.path);
          if (typeof served === 'string' && served.length) return served;
        } catch (e) { log('[modkit] redirect failed for ' + url + ': ' + e.message); }
      }
    }
    return url;
  }

  function isInlineAsset(url) {
    return typeof url === 'string' && (url.indexOf('blob:') === 0 || url.indexOf('data:') === 0);
  }

  function installInterceptors() {

    var names = [EN.loadImage, EN.loadSoundAsync, EN.loadSoundSync, '_HC2', '_OC2', '_QC2'];
    for (var i = 0; i < names.length; i++) {
      (function (name) {
        var orig = window[name];
        if (typeof orig !== 'function') return;
        window[name] = function (a) {
          if (typeof a === 'string' && a.length) a = resolveURL(a);
          return orig.apply(this, arguments.length ? [a].concat(Array.prototype.slice.call(arguments, 1)) : arguments);
        };
      })(names[i]);
    }

    var origNormalize = window[EN.normalizeAssetURL];
    if (typeof origNormalize === 'function') {
      window[EN.normalizeAssetURL] = function (url) {
        if (isInlineAsset(url)) return url;
        return origNormalize.apply(this, arguments);
      };
    }
    var origMakeSoundXhr = window[EN.makeSoundXhr];
    if (typeof origMakeSoundXhr === 'function') {
      window[EN.makeSoundXhr] = function (url, ext) {

        if (isInlineAsset(url) || /^https?:\/\//.test(String(url))) {
          var xhr = new XMLHttpRequest();
          xhr.open('GET', url, true);
          xhr.responseType = 'arraybuffer';
          xhr.URL = url;
          xhr.completed = false;
          return xhr;
        }
        return origMakeSoundXhr.apply(this, arguments);
      };
    }

    // Inline (data:/blob:) images can still be decoding asynchronously when the engine uploads them, and Chrome
    // answers texImage2D(<img>) with INVALID_VALUE. The page's Image property is getter-only (the engine keeps
    // the element in a closure), so the page cannot be swapped for a canvas; instead the upload call itself
    // copies a decoded inline <img> into a canvas and hands the canvas to texImage2D.
    try {
      [window.WebGLRenderingContext, window.WebGL2RenderingContext].forEach(_patchTexImage2D);
    } catch (e) { log('[modkit] texture upload hook unavailable: ' + (e && e.message)); }
  }

  // wraps Ctx.prototype.texImage2D (see the note in the caller); exposed on ModKit for the tests
  function _patchTexImage2D(Ctx) {
    if (!Ctx || !Ctx.prototype || Ctx.prototype.__kitTexPatched) return;
    var origTex = Ctx.prototype.texImage2D;
    Ctx.prototype.texImage2D = function (target, level, internalformat, format, type, source) {
      try {
        if (arguments.length === 6 && source && source.tagName === 'IMG' && isInlineAsset(source.src) &&
            source.complete && source.naturalWidth > 0 && source.naturalHeight > 0) {
          var cv = document.createElement('canvas');
          cv.width = source.naturalWidth;
          cv.height = source.naturalHeight;
          cv.getContext('2d').drawImage(source, 0, 0);
          return origTex.call(this, target, level, internalformat, format, type, cv);
        }
      } catch (e) { /* fall through to the plain upload */ }
      return origTex.apply(this, arguments);
    };
    Ctx.prototype.__kitTexPatched = true;
  }

  function _tableSnapshot() {
    return {
      sprites: gamedata[GD.sprites].length,
      rects: gamedata[GD.frameRects].length,
      pages: gamedata[GD.texturePages].length,
      mips: gamedata[GD.pageMipFlags].length,
      sounds: gamedata[GD.sounds].length,
      objects: gamedata[GD.objects].length,
      groupLens: gamedata[GD.textureGroups].map(function (g) { return [g[GD.groupPages].length, g[GD.groupSprites].length]; }),
      groupCount: gamedata[GD.textureGroups].length,
      forcedRooms: forcedRooms.length,
      redirectKeys: Object.keys(redirectMap),
      startHooks: startHooks.length,
      waitTimers: waitTimers.length,
      menuBackground: menuBackground,
      rooms: gamedata[GD.rooms].map(function (r) {
        var layers = r[GD.roomLayers] || [];
        return {
          inst: r[GD.roomInstances] ? r[GD.roomInstances].length : 0,
          layerIds: layers.map(function (l) { return l[GD.layerInstanceIds] ? l[GD.layerInstanceIds].length : 0; })
        };
      }),
      saveVars: window[EN.saveVarMap] ? Object.keys(window[EN.saveVarMap]) : [],
      roomsLen: gamedata[GD.rooms].length,
      roomListLen: window[EN.roomList] ? window[EN.roomList][EN.roomListArr].length : -1,
      roomOrderLen: window[EN.roomList] ? window[EN.roomList][EN.roomOrderArr].length : -1,
      roomHooks: [roomHooks.start.length, roomHooks.end.length],
      orderListLen: gamedata[GD.roomOrderList].length,
      fnWrapsLen: fnWraps.length,
      menuRowsLen: _menuHooks.rows.length,
      menuTextsLen: _menuHooks.texts.length,
      levelsLen: _levels.length,
      objJournalLen: _objJournal.length,
      eventWrappersLen: _eventWrappers.length,
      settingsLen: _settingsRegistry.length,
      invLen: _invItems.length,
      clueKeys: Object.keys(clueRegistry),
      dialogOpen: !!dialog
    };
  }

  function _rollbackMod(snap) {
    gamedata[GD.sprites].length = snap.sprites;
    gamedata[GD.frameRects].length = snap.rects;
    gamedata[GD.texturePages].length = snap.pages;
    gamedata[GD.pageMipFlags].length = snap.mips;
    gamedata[GD.sounds].length = snap.sounds;
    gamedata[GD.objects].length = snap.objects;
    var groups = gamedata[GD.textureGroups];
    for (var gi = 0; gi < groups.length && gi < snap.groupLens.length; gi++) {
      if (groups[gi][GD.groupPages]) groups[gi][GD.groupPages].length = snap.groupLens[gi][0];
      if (groups[gi][GD.groupSprites]) groups[gi][GD.groupSprites].length = snap.groupLens[gi][1];
    }
    groups.length = snap.groupCount;
    var kept = {};
    for (var k = 0; k < snap.redirectKeys.length; k++) kept[snap.redirectKeys[k]] = 1;
    for (var key in redirectMap) if (redirectMap.hasOwnProperty(key) && !kept[key]) delete redirectMap[key];
    forcedRooms.length = snap.forcedRooms;
    startHooks.length = snap.startHooks;
    waitTimers.length = snap.waitTimers;
    menuBackground = snap.menuBackground;
    gamedata[GD.rooms].forEach(function (r, ri) {
      var sr = snap.rooms[ri];
      if (!sr) return;
      if (r[GD.roomInstances]) r[GD.roomInstances].length = sr.inst;
      var layers = r[GD.roomLayers] || [];
      layers.forEach(function (l, li) {
        if (l[GD.layerInstanceIds] && sr.layerIds[li] !== undefined) l[GD.layerInstanceIds].length = sr.layerIds[li];
      });
    });
    var pM = window[EN.saveVarMap];
    if (pM) for (var v in pM) if (pM.hasOwnProperty(v) && snap.saveVars.indexOf(v) < 0) delete pM[v];

    if (snap.roomListLen >= 0) {
      gamedata[GD.rooms].length = snap.roomsLen;
      window[EN.roomList][EN.roomListArr].length = snap.roomListLen;
      window[EN.roomList][EN.roomOrderArr].length = snap.roomOrderLen;
    }
    roomHooks.start.length = snap.roomHooks[0];
    roomHooks.end.length = snap.roomHooks[1];
    gamedata[GD.roomOrderList].length = snap.orderListLen;

    while (fnWraps.length > snap.fnWrapsLen) {
      var w = fnWraps[fnWraps.length - 1];
      if (window[w.name] === w.fn) window[w.name] = w.prev;
      fnWraps.pop();
    }
    if (typeof snap.menuRowsLen === 'number') {
      while (_menuHooks.rows.length > snap.menuRowsLen) _menuHooks.rows.pop();
      while (_menuHooks.texts.length > snap.menuTextsLen) _menuHooks.texts.pop();
    }
    _levels.length = snap.levelsLen;
    // undo what the failed mod changed on EXISTING objects (wrapEvent / patch), newest first
    while (_objJournal.length > snap.objJournalLen) {
      var j = _objJournal.pop();
      if (j.had) j.entry[j.key] = j.prev; else delete j.entry[j.key];
    }
    _eventWrappers.length = snap.eventWrappersLen;
    _settingsRegistry.length = snap.settingsLen;
    _invItems.length = snap.invLen;
    for (var ck in clueRegistry)
      if (clueRegistry.hasOwnProperty(ck) && snap.clueKeys.indexOf(ck) < 0) delete clueRegistry[ck];
    // the clue marker / viewer objects were registered by the failed mod: they are gone with the object table,
    // so the next clue.add must register them again (and re-wrap the HUD, whose wrap was just undone)
    if (_clueObjIdx >= snap.objects || _clueViewIdx >= snap.objects) {
      _clueObjIdx = -1; _clueViewIdx = -1; _clueHudWrapped = false;
    }
    if (!snap.dialogOpen && dialog) _dialogClose(false);
  }

    function _consoleNamespace(manifest) {
    var meta = manifest.console;
    if (!meta || typeof meta !== 'object') return null;
    var name = String(manifest.name || '').replace(/[^A-Za-z0-9_$]/g, '');
    if (!/^[A-Za-z_$]/.test(name)) return null;
    return { __nsName: name, __declared: meta };
  }
  function _publishConsole(manifest, ns, meta) {
    if (!ns) return;
    var out = {}, n = 0, missing = [];
    for (var k in meta)
      if (Object.prototype.hasOwnProperty.call(meta, k)) {
        if (typeof ns[k] === 'function') { out[k] = ns[k]; n++; }
        else missing.push(k);
      }
    if (n === 0) {
      if (missing.length) log('[modkit] ' + manifest.name + ': console commands declared but none implemented: ' + missing.join(', '));
      return;
    }
    if (window[ns.__nsName] !== undefined) {
      console.error('[modkit] ' + manifest.name + ': console namespace "' + ns.__nsName + '" collides with an existing global - not published');
      return;
    }
    out.help = function () {
      log('[modkit] ' + ns.__nsName + ' console commands:');
      for (var k2 in meta)
        if (Object.prototype.hasOwnProperty.call(meta, k2))
          log('  ' + ns.__nsName + '.' + k2 + ' - ' + meta[k2]);
      return Object.keys(out).filter(function (x) { return x !== 'help'; });
    };
    window[ns.__nsName] = out;
    log('[modkit] ' + manifest.name + ': console namespace "' + ns.__nsName + '" published (' + n + ' command' + (n > 1 ? 's' : '') + ')');
  }

// optional manifest "icon": a 16x16 or 32x32 png shown in the mods menu.
// Registered for disabled mods too (like a resource pack's pack.png).
function _loadModIcon(manifest, api) {
  if (typeof manifest.__iconSprite === 'number') return;
  if (typeof manifest.icon !== 'string' || !manifest.icon) return;
  try {
    var a = api || makeAPI(manifest, manifest.__files);
    var ico = a.sprite.add({ name: 'spr_ModIcon_' + manifest.name, file: manifest.icon });
    manifest.__iconSprite = ico.index;
  } catch (ie) { log('[modkit] ' + manifest.name + ': icon "' + manifest.icon + '" not loaded: ' + ie.message); }
}

  // "file.js:LINE" for an error thrown while a mod's scripts were running as one concatenated function
  // (V8: "<anonymous>:L:C" with 2 header lines from new Function; Firefox: "Function:L:C"). Falls back to
  // the entry file name when the stack has no usable position. The offending source line is appended.
  function _whereInMod(e, segs, code, main) {
    try {
      var m = /eval at runMod[^\n]*<anonymous>:(\d+):\d+\)/.exec(String(e && e.stack)) || /Function:(\d+):\d+/.exec(String(e && e.stack));
      if (!m) return main;
      var line = parseInt(m[1], 10) - 2;
      var seg = segs[0];
      for (var i = 0; i < segs.length; i++) if (segs[i].start <= line) seg = segs[i];
      var text = (code.split('\n')[line - 1] || '').trim();
      if (text.length > 70) text = text.slice(0, 69) + '...';
      return seg.file + ':' + (line - seg.start + 1) + (text ? ' [' + text + ']' : '');
    } catch (e2) { return main; }
  }

function runMod(manifest, files) {
    var main = manifest.main || 'init.js';
    if (!files[main]) throw new Error('missing entry ' + main);
    var scripts = manifest.scripts || [];
    if (typeof scripts === 'string') scripts = [scripts];
    if (!Array.isArray(scripts)) throw new Error('manifest "scripts" must be an array of file names');
    var code = decodeText(files[main]);
    // line map of the concatenated source, so an error can be reported as file:line
    var segs = [{ file: main, start: 1 }];
    // manifest "levels": the kit builds the level rooms right after init.js
    // and before the "scripts" entries (see 45_levels.js); the call is injected
    // as a hidden parameter so it never shows up on ModKit
    var hasLevels = manifest.levels !== undefined && manifest.levels !== null;
    if (hasLevels) code += '\n;\n__kitBuildLevels__();\n;\n';
    for (var si = 0; si < scripts.length; si++) {
      var sn = scripts[si];
      if (typeof sn !== 'string' || !sn) throw new Error('manifest "scripts" entries must be non-empty file names');
      if (!files[sn]) throw new Error('missing script file ' + sn);
      var snCode = decodeText(files[sn]);
      try { (new Function(snCode)); }
      catch (e) { throw new Error('syntax error in ' + sn + ': ' + (e && e.message ? e.message : e)); }
      code += '\n;\n';
      segs.push({ file: sn, start: code.split('\n').length });
      code += snCode;
    }
    var api = makeAPI(manifest, files);
    manifest.__fileURL = api.fileURL;
    var cmdNS = _consoleNamespace(manifest);
    var modMainFn;
    try { modMainFn = new Function('ModKit', 'globalThis', 'Commands', '__kitBuildLevels__', code); }
    catch (e) { delete manifest.__fileURL; throw new Error('syntax error in ' + main + ': ' + e.message); }
    var snap = _tableSnapshot();
    var levelsBuilt = false;
    // a throw from deep inside the kit (sprite loading, room building...) pushes the mod's own frame past V8's
    // default 10-frame stack limit; widen it while the mod runs so the file:line lookup still finds it
    var _oldTraceLimit = Error.stackTraceLimit;
    try { Error.stackTraceLimit = 60; } catch (e0) { }
    try {
      modMainFn(api, window, cmdNS, function () {
        if (levelsBuilt) return;
        levelsBuilt = true;
        _setupLevels(manifest, files, api);
      });
      if (hasLevels && !levelsBuilt) { levelsBuilt = true; _setupLevels(manifest, files, api); }
      _loadModIcon(manifest, api);
    }
    catch (e) {
      try { Error.stackTraceLimit = _oldTraceLimit; } catch (e1) { }
      _rollbackMod(snap);
      delete manifest.__fileURL;
      throw new Error(_whereInMod(e, segs, code, main) + ' threw: ' + (e && e.message ? e.message : e) + ' (all changes rolled back)');
    }
    try { Error.stackTraceLimit = _oldTraceLimit; } catch (e2) { }

    if (manifest.__applyOverrides) {
      var ovs = manifest.__applyOverrides();
      if (ovs.length) log('[modkit] ' + manifest.name + ': ' + ovs.length +
        ' sprite override(s): ' + ovs.join(', '));
    }
    manifest.__commands = cmdNS;
    _publishConsole(manifest, cmdNS, manifest.console || null);
    loadedMods.push(manifest);
    log('[modkit] loaded mod "' + manifest.name + '" v' + (manifest.version || '?') +
        (manifest.title ? ' - ' + manifest.title : ''));
  }

  function decodeManifestText(raw) {
    return parseJSONC(decodeText(raw));
  }

  function applyKitMenuBackground() {
    var info = window.__MODKIT_MENU_BG__;
    if (!info || !info.page) return;
    try {
      var reg = _getKitAPI().sprite.addFile({ name: 'modkit_menu_bg',
        page: info.page, width: info.width, height: info.height });
      setMenuBackgroundInternal(reg.index, { fit: 'stretch', dim: 0.55 });
      log('[modkit] menu background applied (html5game/' + info.page + ', sprite ' + reg.index + ')');
    } catch (e) {
      log('[modkit] menu background failed: ' + (e && e.message));
    }
  }

  function resolveModOrder(list, disabledList) {
    var byName = {};
    for (var i = 0; i < list.length; i++) byName[list[i].name] = i;
    var runnable = {}, inStack = {}, order = [], skipped = [], problems = [];
    function visit(mi) {
      if (runnable[mi] !== undefined) return runnable[mi];
      if (inStack[mi]) {
        problems.push('circular dependency through "' + list[mi].name + '"');
        return (runnable[mi] = false);
      }
      inStack[mi] = true;
      var ok = true;
      var deps = list[mi].dependencies || [];
      for (var d = 0; d < deps.length; d++) {
        var dn = typeof deps[d] === 'string' ? deps[d] : (deps[d] && deps[d].name);
        if (!dn) continue;
        var di = byName[dn];
        if (di === undefined) { problems.push('"' + list[mi].name + '" depends on missing "' + dn + '"'); ok = false; }
        else if (disabledList.indexOf(dn) >= 0) { problems.push('"' + list[mi].name + '" depends on disabled "' + dn + '"'); ok = false; }
        else if (!visit(di)) ok = false;
      }
      inStack[mi] = false;
      runnable[mi] = ok;
      if (ok) order.push(mi); else skipped.push(list[mi].name);
      return ok;
    }
    for (var s = 0; s < list.length; s++) visit(s);
    return { order: order, skipped: skipped, problems: problems };
  }

  // ---- data/rooms overlay (baked into mods_bundled.js at build time) -----
  // Mirrors the debug loader's merge: {__fn:"name"} placeholders resolve to
  // live window functions; arrays merge element-wise; objects merge by key.
  function _fnPlaceholder(o) {
    return o !== null && typeof o === 'object' && !Array.isArray(o) &&
      Object.keys(o).length === 1 && o.__fn !== undefined;
  }
  function _overlayData(dst, src, path) {
    if (_fnPlaceholder(src)) {
      var fn = window[src.__fn];
      if (typeof fn !== 'function')
        throw new Error(path + ': function not found on window: ' + src.__fn +
          ' (data/rooms JSON referencing a function that does not exist?)');
      return fn;
    }
    if (Array.isArray(src)) {
      var arr = Array.isArray(dst) ? dst : [];
      for (var i = 0; i < src.length; i++) arr[i] = _overlayData(arr[i], src[i], path + '[' + i + ']');
      return arr;
    }
    if (src !== null && typeof src === 'object') {
      var out = (dst !== null && typeof dst === 'object' && !Array.isArray(dst)) ? dst : {};
      for (var k in src) {
        if (!Object.prototype.hasOwnProperty.call(src, k)) continue;
        out[k] = _overlayData(out[k], src[k], path + '.' + k);
      }
      return out;
    }
    return src;
  }
  function _applyRoomOverlay() {
    var ov = window.__MODKIT_ROOM_OVERLAY__;
    if (!ov || !ov.roomsB64) return 0;
    var bytes = base64ToBytes(ov.roomsB64);
    var files = readZip(bytes.buffer ? bytes.buffer.slice(0, bytes.length) : bytes);
    var names = [];
    for (var k in files) if (Object.prototype.hasOwnProperty.call(files, k)) names.push(k);
    names.sort();
    var applied = 0, failed = 0;
    for (var ni = 0; ni < names.length; ni++) {
      var name = names[ni];
      try {
        var room = JSON.parse(decodeText(files[name]));
        if (!room || typeof room !== 'object' || typeof room.id !== 'number' || !room.raw)
          throw new Error('not a room dump (missing id/raw)');
        if (!gamedata.rooms[room.id])
          throw new Error('id ' + room.id + ' out of range (gamedata has ' + gamedata.rooms.length + ' rooms)');
        gamedata.rooms[room.id] = _overlayData(gamedata.rooms[room.id], room.raw, 'rooms.' + room.id);
        applied++;
      } catch (e) {
        failed++;
        log('[modkit] data/rooms overlay: ' + name + ' FAILED: ' + (e && e.message ? e.message : e) +
          ' — room stays stock, fix data/rooms/ and rebuild');
      }
    }
    if (applied) log('[modkit] data/rooms overlay: ' + applied + ' room(s) applied from data/ (baked at build time)' +
      (failed ? (', ' + failed + ' FAILED (see above)') : ''));
    return applied;
  }

  // The kit talks to the engine through mangled names (10_engine_map.js). They only match the exact Lost.js this
  // kit was built against, so check them once at startup and name what is missing instead of failing later.
  function _engineMapCheck(running) {
    var missing = [];
    ['normalizeAssetURL', 'makeSoundXhr', 'loadImage', 'loadSoundAsync', 'loadSoundSync'].forEach(function (k) {
      if (typeof window[EN[k]] !== 'function') missing.push('EN.' + k + ' (' + EN[k] + ')');
    });
    // the room store and room constants only exist once the game is running (checked again on the first steps)
    if (running) {
      if (!window[EN.roomConstants] || typeof window[EN.roomConstants][EN.currentRoomFn] !== 'function')
        missing.push('EN.currentRoomFn on EN.roomConstants (' + EN.roomConstants + '.' + EN.currentRoomFn + ')');
      if (typeof window[EN.roomList] !== 'object' || !window[EN.roomList]) missing.push('EN.roomList (' + EN.roomList + ')');
    }
    ['objects', 'sprites', 'sounds', 'rooms', 'texturePages', 'frameRects', 'textureGroups'].forEach(function (k) {
      if (!gamedata || !Array.isArray(gamedata[GD[k]])) missing.push('GD.' + k + ' (' + GD[k] + ')');
    });
    if (missing.length)
      loud('ENGINE MAP MISMATCH - this ModKit was built for a different Lost.js; missing: ' + missing.join(', ') +
        '. Rebuild with build.py (it verifies the engine) before trusting any mod.');
    return missing;
  }

  function bootstrap(startGame) {
    gamedata = window.gamedata || null;
    try { _engineMapCheck(); } catch (e) { log('[modkit] engine map check failed: ' + (e && e.message)); }

    if (gamedata) { try { _applyRoomOverlay(); } catch (e) { log('[modkit] data/rooms overlay failed: ' + (e && e.message)); } }
    if (gamedata) { try { _registerDialogOverlay(); }
      catch (e) { log('[modkit] dialog overlay registration failed: ' + (e && e.message)); } }
    if (gamedata) { try { _registerClueObjects(); }
      catch (e) { log('[modkit] clue object registration failed: ' + (e && e.message)); } }
    if (!gamedata) {
      log('[modkit] FATAL: gamedata not found - is Lost.js loaded before modkit.js?');
      if (startGame) startGame();
      return false;
    }
    var disabled = getDisabled();
    var bundles = window.__MODKIT_BUNDLED || [];
    for (var i = 0; i < bundles.length; i++) {
      try {
        var modBytes = base64ToBytes(bundles[i].b64);
        var files = readZip(modBytes.buffer.slice(0, modBytes.length) || modBytes);
        if (!files['package.jsonc']) throw new Error('no package.jsonc in ' + bundles[i].name);
        var manifest = decodeManifestText(files['package.jsonc']);
        if (!manifest.name) throw new Error('manifest without a name');
        manifest.__files = files;
        allMods.push(manifest);
      } catch (e) {
        log('[modkit] cannot read bundled mod ' + (bundles[i] && bundles[i].name) + ': ' + e.message);
      }
    }
    captureStockCounts();

    var resolved = resolveModOrder(allMods, disabled);
    for (var pi = 0; pi < resolved.problems.length; pi++)
      log('[modkit] dependency: ' + resolved.problems[pi]);
    for (var ski = 0; ski < resolved.skipped.length; ski++)
      log('[modkit] mod "' + resolved.skipped[ski] + '" SKIPPED (unmet dependencies)');
    for (i = 0; i < resolved.order.length; i++) {
      var modEntry = allMods[resolved.order[i]];
      if (disabled.indexOf(modEntry.name) >= 0) {
        log('[modkit] skipping disabled mod "' + modEntry.name + '"');
        _loadModIcon(modEntry);
        continue;
      }
      try { runMod(modEntry, modEntry.__files); }
      catch (e) {
        modEntry.__failed = String(e && e.message ? e.message : e);
        loud('mod "' + modEntry.name + '" failed (rolled back)', e);
      }
    }

    try {
      window.addEventListener('error', function (ev) {
        loud('uncaught error' + (ev.target && ev.target.src ? ' (resource: ' + ev.target.src + ')' : ''),
          ev.error || ev.message);
        return false;
      });
      window.addEventListener('unhandledrejection', function (ev) {
        loud('unhandled rejection', ev.reason);
      });
    } catch (e) { log('[modkit] error trappers unavailable: ' + (e && e.message)); }

    if (window.__MODKIT_DEBUG__) installGLSpy();
    else window.__modkitGL = { enabled: false,
      note: 'GL spy is off - build with --debug to enable texture-upload logging' };
    installInterceptors();
    installControllerTick();
    installRoomEventHooks();
    applyKitMenuBackground();
    installModMenu();
    installPauseMenu();
    installHudHints();

    var kitApi = _getKitAPI();
    for (var k in kitApi)
      if (kitApi.hasOwnProperty(k) && window.ModKit[k] === undefined)
        window.ModKit[k] = kitApi[k];
    log('[modkit] boot: ' + loadedMods.length + '/' + allMods.length + ' mod(s) active');
    if (startGame) startGame();
    return true;
  }

  window.ModKit = {
    bootstrap: bootstrap,

    describe: _describe,
    resources: _resources,
    tables: _tables,

    _forcedRoomsPending: function () { return forcedRooms.length; },
    _exitDirection: function () { return _exitDirection(); },
    _depOrder: resolveModOrder,
    _dialogTick: function (fakeSelf) { dialogTick(fakeSelf || { pressed_Z: 0 }); },
    _dialogDraw: function () { dialogDraw(); },
    _pendingRemovals: function () { return _pendingRoomRemovals.slice(); },
    _flushRoomRemovals: function () { _runPendingRoomRemovals(); },
    _patchTexImage2D: function (Ctx) { _patchTexImage2D(Ctx); },
    _engineMapCheck: function () { return _engineMapCheck(true); },
    _levelTick: function () { _levelTick(); },
    _levelFeed: function (side) { _levelFeedExit(_levelSide(side, '_levelFeed')); },
    _levelState: function () {
      return { origin: _levelOrigin, levels: _levels.map(function (l) {
        return { mod: l.mod, id: l.id, room: l.roomId, pattern: l.pattern && l.pattern.slice(), progress: l.progress,
          armed: !!l.armEntry, spent: l.spent, once: l.once, exit: l.exit }; }) };
    },
    _dialogState: function () { return dialog ? (dialog.stock ? 'stock' : dialog.lines[dialog.idx].text.slice(0, dialog.chars)) : null; },

    list: function () {
      var disabled = getDisabled(), out = [];
      for (var i = 0; i < allMods.length; i++) {
        var modInfo = allMods[i];
        out.push({ name: modInfo.name, title: modInfo.title || modInfo.name, version: modInfo.version || '',
                   author: modInfo.author || '', description: modInfo.description || '',
                   icon: (typeof modInfo.__iconSprite === 'number') ? modInfo.__iconSprite : -1,
                   enabled: disabled.indexOf(modInfo.name) < 0 });
      }
      return out;
    },

    setMenuBackground: function (spriteIndex, opts) { return setMenuBackgroundInternal(spriteIndex, opts); },
    clearMenuBackground: function () { menuBackground = null; },

    setEnabled: function (name, on) {
      var disabledList = getDisabled(), foundAt = disabledList.indexOf(name);
      if (on && foundAt >= 0) disabledList.splice(foundAt, 1);
      if (!on && foundAt < 0) disabledList.push(name);
      setDisabled(disabledList);
    },

    parseJSONC: parseJSONC,
    inflate: inflate,
    readZip: readZip,
    crc32: crc32,
    resolveURL: resolveURL,
    _redirectMap: redirectMap,
    _loadedMods: loadedMods,
    _bootLog: bootLog,
    _makeAPI: makeAPI,
    _gamedata: function () { return gamedata; },

    _gameFPS: function () { return gameFPS(); },
    decodeText: decodeText,
    _runMod: runMod,
    _stepTimers: function (dt) { stepTimers(dt === undefined ? (1 / gameFPS()) : dt); },

    _liveWrap: function (objName, eventName, wrapper) {
      var classTable = window[EN.classRegistry] && window[EN.classRegistry][EN.classTable];
      if (!classTable) throw new Error('ModKit._liveWrap: game not booted yet (no runtime class table)');
      var idx = -1, gd = gamedata;
      if (gd) for (var i = 0; i < gd[GD.objects].length; i++) {
        if (gd[GD.objects][i] && gd[GD.objects][i][GD.name] === objName) { idx = i; break; }
      }
      if (idx < 0 || !classTable[idx]) throw new Error('ModKit._liveWrap: no runtime class for object ' + objName);
      var key = eventName.indexOf('ev_') === 0 ? eventName : 'ev_' + eventName;
      var runtimeClass = classTable[idx];
      var orig = runtimeClass[key];
      if (typeof orig !== 'function') {
        throw new Error('ModKit._liveWrap: ' + objName + ' has no ' + key +
                        ' (new events cannot be registered after boot)');
      }
      runtimeClass[key] = function (self, other) { return wrapper(orig, self, other); };
      return function () { runtimeClass[key] = orig; };
    }
  };

;

  function installGLSpy() {
    if (!window.HTMLCanvasElement || !window.HTMLCanvasElement.prototype) return;
    var proto = window.HTMLCanvasElement.prototype;
    var origGetContext = proto.getContext;
    var stats = { webgl: false, frames: 0, drawArrays: 0, uploads: [], badUploads: 0, summary: '' };
    var wrappedCtxs = (typeof WeakSet === 'function') ? new WeakSet() : null;
    window.__modkitGL = stats;

    proto.getContext = function (type) {
      var ctx = origGetContext.apply(this, arguments);
      var isGL = /^(webgl|experimental-webgl|moz-webgl|webkit-3d)/.test(String(type || ''));
      if (!ctx || !isGL) return ctx;
      if (wrappedCtxs && wrappedCtxs.has(ctx)) return ctx;
      var realGL = (typeof window.WebGLRenderingContext !== 'undefined') &&
                   (ctx instanceof window.WebGLRenderingContext);
      if (!wrappedCtxs) return ctx;
      wrappedCtxs.add(ctx);
      stats.webgl = true;

      var origTexImage2D = ctx.texImage2D && ctx.texImage2D.bind(ctx);
      var origDrawArrays = ctx.drawArrays && ctx.drawArrays.bind(ctx);
      var origBindTexture = ctx.bindTexture && ctx.bindTexture.bind(ctx);

      function describeSource(src) {
        try {
          if (src === null) return { w: 'null', h: 'null', complete: true, desc: 'null-source', isNull: true };
          if (!src) return { w: 0, h: 0, complete: false, desc: 'undefined-source' };
          var w = src.videoWidth || src.naturalWidth || src.width;
          var h = src.videoHeight || src.naturalHeight || src.height;
          var desc = (src.src !== undefined) ? String(src.src).slice(0, 70)
                    : (src.tagName ? ('<' + src.tagName.toLowerCase() + '>') : (src instanceof window.Uint8Array ? ('typed-array ' + src.length + 'B') : typeof src));
          return { w: w, h: h, complete: src.complete === undefined ? true : !!src.complete, desc: desc };
        } catch (e) { return { w: '?', h: '?', complete: '?', desc: 'unreadable' }; }
      }

      if (origTexImage2D) {
        ctx.texImage2D = function () {
          var argc = arguments.length;
          var src = (argc >= 9) ? arguments[8]
                               : arguments[5];
          var info = describeSource(src);
          info.argc = argc;
          stats.uploads.push(info);

          log('[modkit][GL] upload#' + stats.uploads.length + ' argc=' + argc + ' ' +
              (info.isNull ? 'null source' : (info.w + 'x' + info.h + ' ' + info.desc.slice(0, 50))));

          var glErr = 0;
          if (realGL) { try { glErr = ctx.getError(); } catch (e) { glErr = -1; } }
          if (glErr) log('[modkit][GL] upload#' + stats.uploads.length + ' GL ERROR 0x' +
            glErr.toString(16) + ' — Chrome REJECTED this upload (it drew nothing / drew black)');

          var bad = false;
          if (argc < 9 && (info.isNull || src === undefined)) {
            bad = true;
            if (realGL) log('[modkit][GL] BAD upload: null/undefined source in the SHORT texImage2D form (argc=' + argc +
              ') — Chrome rejects it, the texture gets no storage and draws nothing');
          } else if (src && (!info.w || !info.h)) {
            bad = true;
            if (realGL) log('[modkit][GL] BAD upload: source image has no size (' + info.w + 'x' + info.h + ' ' + info.desc.slice(0, 50) +
              ') — never decoded (bad MIME type or undecodable file), the sprite will draw black');
          }
          if (bad) {
            stats.badUploads++;
            try { stats.lastBadStack = String((new Error()).stack).split('\n').slice(1, 5).join(' <- '); } catch (e) {}
          }
          var uploaded = origTexImage2D.apply(ctx, arguments);

          if (realGL) {
            try {
              var postErr = ctx.getError();
              if (postErr) {
                stats.badUploads++;
                log('[modkit][GL] upload#' + stats.uploads.length +
                  ' REJECTED by the GL (error 0x' + postErr.toString(16) +
                  ', source ' + (info.isNull ? 'null' : info.w + 'x' + info.h + ' ' + info.desc.slice(0, 30)) + ')' +
                  ' — the texture stayed empty; this sprite draws nothing');
              }
            } catch (e) {}
          }
          return uploaded;
        };
      }
      if (origDrawArrays) {
        ctx.drawArrays = function () { stats.drawArrays++; return origDrawArrays.apply(ctx, arguments); };
      }
      if (origBindTexture) {
        var bindCount = 0;
        ctx.bindTexture = function () { bindCount++; stats.lastBinds = bindCount; return origBindTexture.apply(ctx, arguments); };
      }

      if (realGL) {
        var origRAF = window.requestAnimationFrame || function (cb) { return setTimeout(cb, 16); };
        window.requestAnimationFrame = function (cb) {
          return origRAF(function () { stats.frames++; return cb.apply(this, arguments); });
        };
        var report = function () {
          var last = stats.uploads.length ? stats.uploads[stats.uploads.length - 1].desc : 'none';
          stats.summary = 'frames=' + stats.frames + ' drawArrays=' + stats.drawArrays +
            ' textureUploads=' + stats.uploads.length + ' badUploads=' + stats.badUploads;
          log('[modkit][GL] ' + stats.summary + ' | last upload: ' + last);
          setTimeout(report, 5000);
        };
        setTimeout(report, 5000);
      }
      return ctx;
    };
  }

})();

