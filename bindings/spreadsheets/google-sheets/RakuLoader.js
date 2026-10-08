// RakuLoader.gs — loads the Raku engine inside Apps Script.
//
// Apps Script cannot hold a binary file, so build.raku stores the engine's
// WebAssembly gzip-compressed and base64-encoded, split across the RakuWasm*
// files. Each of those sets one slot of RAKUSHEET_WASM, whatever order the
// project runs its files in. Unpacking is done here in plain JavaScript:
// Apps Script has neither atob nor a decompression stream.

var RAKUSHEET_WASM_COUNT = @WASM_COUNT@;
var RAKUSHEET_ENGINE = null;

// The engine, loaded once per execution. Sheets may or may not reuse an
// execution for the next formula; when it does, the load is skipped.
function rakuSheetEngine() {
  if (RAKUSHEET_ENGINE) return RAKUSHEET_ENGINE;
  RakuSheet.installShims(globalThis);
  var parts = [];
  for (var i = 0; i < RAKUSHEET_WASM_COUNT; i++) {
    var part = typeof RAKUSHEET_WASM === 'undefined' ? undefined : RAKUSHEET_WASM[i];
    if (typeof part !== 'string') {
      throw new Error('the Raku engine is incomplete: file RakuWasm' + rakuSheetPad(i + 1) + ' is missing');
    }
    parts.push(part);
  }
  var wasm = rakuSheetGunzip(rakuSheetBase64(parts.join('')));
  RAKUSHEET_ENGINE = RakuSheet.start(RakuJS, { wasmBytes: wasm }).catch(function (e) {
    RAKUSHEET_ENGINE = null;
    throw e;
  });
  return RAKUSHEET_ENGINE;
}

function rakuSheetPad(n) {
  return (n < 10 ? '0' : '') + n;
}

function rakuSheetBase64(s) {
  var alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  var t = new Uint8Array(128);
  for (var k = 0; k < 64; k++) t[alphabet.charCodeAt(k)] = k;
  var n = s.length;
  while (n > 0 && s.charAt(n - 1) === '=') n--;
  var out = new Uint8Array((n * 3) >> 2);
  var o = 0, i = 0, v;
  for (; i + 4 <= n; i += 4) {
    v = t[s.charCodeAt(i)] << 18 | t[s.charCodeAt(i + 1)] << 12 | t[s.charCodeAt(i + 2)] << 6 | t[s.charCodeAt(i + 3)];
    out[o++] = v >> 16; out[o++] = (v >> 8) & 255; out[o++] = v & 255;
  }
  if (n - i === 2) {
    v = t[s.charCodeAt(i)] << 18 | t[s.charCodeAt(i + 1)] << 12;
    out[o++] = v >> 16;
  } else if (n - i === 3) {
    v = t[s.charCodeAt(i)] << 18 | t[s.charCodeAt(i + 1)] << 12 | t[s.charCodeAt(i + 2)] << 6;
    out[o++] = v >> 16; out[o++] = (v >> 8) & 255;
  }
  return out;
}

// gzip (RFC 1952) around DEFLATE (RFC 1951). The trailer gives the size to
// allocate and the CRC that tells a damaged paste from a good one.
function rakuSheetGunzip(b) {
  if (b[0] !== 0x1f || b[1] !== 0x8b || b[2] !== 8) throw new Error('the Raku engine data is not gzip');
  var flags = b[3], p = 10;
  if (flags & 4) p += 2 + (b[p] | b[p + 1] << 8);
  if (flags & 8) while (b[p++]) {}
  if (flags & 16) while (b[p++]) {}
  if (flags & 2) p += 2;
  var e = b.length;
  var crc = (b[e - 8] | b[e - 7] << 8 | b[e - 6] << 16 | b[e - 5] << 24) >>> 0;
  var size = (b[e - 4] | b[e - 3] << 8 | b[e - 2] << 16 | b[e - 1] << 24) >>> 0;
  var out = new Uint8Array(size);
  rakuSheetInflate(b, p, out);
  if (rakuSheetCrc32(out) !== crc) throw new Error('the Raku engine data is damaged (CRC mismatch); paste the RakuWasm files again');
  return out;
}

function rakuSheetCrc32(bytes) {
  var table = new Int32Array(256);
  for (var n = 0; n < 256; n++) {
    var c = n;
    for (var k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c;
  }
  var crc = -1;
  for (var i = 0; i < bytes.length; i++) crc = table[(crc ^ bytes[i]) & 255] ^ (crc >>> 8);
  return (crc ^ -1) >>> 0;
}

// DEFLATE, after Mark Adler's puff.c: canonical Huffman codes decoded one
// bit at a time, which keeps the code short and is fast enough for one
// engine-sized file per execution.
function rakuSheetInflate(src, start, out) {
  var pos = start, bitbuf = 0, bitcnt = 0, outpos = 0;
  var LBASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
  var LEXT = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
  var DBASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
  var DEXT = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
  var ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];

  function fail(what) { throw new Error('the Raku engine data is damaged (' + what + ')'); }

  function bits(need) {
    var val = bitbuf;
    while (bitcnt < need) {
      if (pos >= src.length) fail('input ends early');
      val |= src[pos++] << bitcnt;
      bitcnt += 8;
    }
    bitbuf = val >>> need;
    bitcnt -= need;
    return val & ((1 << need) - 1);
  }

  function huffman(size) { return { count: new Int16Array(16), symbol: new Int16Array(size) }; }

  function construct(h, lengths, offset, n) {
    var offs = new Int16Array(16), len, sym;
    for (len = 0; len < 16; len++) h.count[len] = 0;
    for (sym = 0; sym < n; sym++) h.count[lengths[offset + sym]]++;
    for (len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
    for (sym = 0; sym < n; sym++) {
      if (lengths[offset + sym] !== 0) h.symbol[offs[lengths[offset + sym]]++] = sym;
    }
  }

  function decode(h) {
    var code = 0, first = 0, index = 0, count;
    for (var len = 1; len < 16; len++) {
      if (bitcnt === 0) {
        if (pos >= src.length) fail('input ends early');
        bitbuf = src[pos++];
        bitcnt = 8;
      }
      code |= bitbuf & 1;
      bitbuf >>>= 1;
      bitcnt--;
      count = h.count[len];
      if (code - count < first) return h.symbol[index + (code - first)];
      index += count;
      first = (first + count) << 1;
      code <<= 1;
    }
    fail('bad code');
  }

  function codes(lencode, distcode) {
    for (;;) {
      var sym = decode(lencode);
      if (sym < 256) {
        out[outpos++] = sym;
      } else if (sym === 256) {
        return;
      } else {
        sym -= 257;
        if (sym >= 29) fail('bad length');
        var len = LBASE[sym] + bits(LEXT[sym]);
        sym = decode(distcode);
        if (sym >= 30) fail('bad distance');
        var dist = DBASE[sym] + bits(DEXT[sym]);
        if (dist > outpos || outpos + len > out.length) fail('bad copy');
        var from = outpos - dist;
        while (len--) out[outpos++] = out[from++];
      }
    }
  }

  function stored() {
    bitbuf = 0;
    bitcnt = 0;
    if (pos + 4 > src.length) fail('input ends early');
    var len = src[pos] | src[pos + 1] << 8;
    var nlen = src[pos + 2] | src[pos + 3] << 8;
    pos += 4;
    if (len !== (~nlen & 0xffff)) fail('bad stored block');
    if (pos + len > src.length || outpos + len > out.length) fail('bad stored block');
    out.set(src.subarray(pos, pos + len), outpos);
    pos += len;
    outpos += len;
  }

  var fixedLen = huffman(288), fixedDist = huffman(30);
  (function () {
    var l = new Int16Array(288), s;
    for (s = 0; s < 144; s++) l[s] = 8;
    for (; s < 256; s++) l[s] = 9;
    for (; s < 280; s++) l[s] = 7;
    for (; s < 288; s++) l[s] = 8;
    construct(fixedLen, l, 0, 288);
    for (s = 0; s < 30; s++) l[s] = 5;
    construct(fixedDist, l, 0, 30);
  })();

  var lencode = huffman(288), distcode = huffman(32), lengths = new Int16Array(320);

  function dynamic() {
    var nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4, index;
    if (nlen > 286 || ndist > 30) fail('bad counts');
    for (index = 0; index < ncode; index++) lengths[ORDER[index]] = bits(3);
    for (; index < 19; index++) lengths[ORDER[index]] = 0;
    construct(lencode, lengths, 0, 19);
    index = 0;
    while (index < nlen + ndist) {
      var symbol = decode(lencode), len = 0;
      if (symbol < 16) {
        lengths[index++] = symbol;
        continue;
      }
      if (symbol === 16) {
        if (index === 0) fail('repeat with no first length');
        len = lengths[index - 1];
        symbol = 3 + bits(2);
      } else if (symbol === 17) {
        symbol = 3 + bits(3);
      } else {
        symbol = 11 + bits(7);
      }
      if (index + symbol > nlen + ndist) fail('too many lengths');
      while (symbol--) lengths[index++] = len;
    }
    construct(lencode, lengths, 0, nlen);
    construct(distcode, lengths, nlen, ndist);
    codes(lencode, distcode);
  }

  var last;
  do {
    last = bits(1);
    var type = bits(2);
    if (type === 0) stored();
    else if (type === 1) codes(fixedLen, fixedDist);
    else if (type === 2) dynamic();
    else fail('bad block type');
  } while (!last);
  if (outpos !== out.length) fail('wrong size');
}
