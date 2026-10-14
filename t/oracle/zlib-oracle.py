"""The other side of t/oracle/zlib.raku: real libz (Python's zlib module is a
binding to it) and the system gzip, which has its own deflate and inflate.

    python3 -I zlib-oracle.py gen   DIR SEED COUNT   write bodies, their libz and
                                                     gzip streams, and mutants
    python3 -I zlib-oracle.py check DIR              inflate every stream WE wrote
                                                     and compare it to its body

Everything a round needs is in DIR, named <body>.<what>, so a failure can be
replayed from the files alone:

    <k>.in                   the body
    <k>.desc                 how it was generated, one line
    <k>.L.<wrap>.<tag>       libz's (or gzip's) stream for it, wrap = zlib|raw|gzip
    <k>.M<i>.<wrap>          a mutant of one of those streams
    <k>.M<i>.<wrap>.want     what libz made of the mutant: the bytes, or absent
                             when libz refused it
    <k>.O.<wrap>.<level>     OUR stream, written by the driver, read by `check`

The bodies are generated here rather than in Raku only for speed; nothing about
them needs to be independent of the engine. What must be independent is the
codec, and that is libz and gzip.
"""

import os
import random
import subprocess
import sys
import zlib

WBITS = {'zlib': 15, 'raw': -15, 'gzip': 31}


# ---- bodies ----------------------------------------------------------------
#
# Each generator aims at a path a fixed corpus does not reach. Sizes are drawn
# on a log scale and the boundaries the format has (the 32 KB window, the 64 KB
# stored block) are drawn on purpose.

def rb(r, n):
    # random.randbytes is 3.9; this is the same thing on 3.8.
    return r.getrandbits(8 * n).to_bytes(n, 'little') if n > 0 else b''


def fib_weights(n):
    a, b, w = 1, 1, []
    for _ in range(n):
        w.append(a)
        a, b = b, a + b
    return w


def g_random(r, n):
    return rb(r, n), 'uniform random bytes'


def g_text(r, n):
    # Zipf-ish words: a skewed literal alphabet with plenty of matches.
    vocab = [''.join(r.choice('etaoinshrdlucmfwypvbgkqjxz') for _ in range(r.randint(1, 9)))
             for _ in range(r.randint(20, 2000))]
    weights = [1.0 / (i + 1) for i in range(len(vocab))]
    out, size = [], 0
    while size < n:
        w = r.choices(vocab, weights)[0]
        sep = r.choices([' ', ', ', '. ', '\n'], [80, 8, 8, 4])[0]
        out.append(w + sep)
        size += len(w) + len(sep)
    return ''.join(out).encode()[:n], 'zipf words over %d' % len(vocab)


def g_logs(r, n):
    # What found #159: varied log lines, a megabyte of them grows a literal
    # tree deeper than 15.
    levels = ['INFO', 'WARN', 'DEBUG', 'ERROR', 'TRACE']
    paths = ['/api/v%d/%s' % (r.randint(1, 3), r.choice(['users', 'orders', 'items', 'auth', 'search']))
             for _ in range(40)]
    out, size, t = [], 0, r.randint(1_600_000_000, 1_700_000_000)
    while size < n:
        t += r.randint(0, 5)
        line = '%d %s %d.%d.%d.%d %s %s %d %dms req=%08x\n' % (
            t, r.choice(levels), r.randint(1, 255), r.randint(0, 255), r.randint(0, 255),
            r.randint(0, 255), r.choice(['GET', 'POST', 'PUT', 'DELETE']), r.choice(paths),
            r.choice([200, 200, 200, 201, 204, 301, 404, 500]), r.randint(0, 5000), r.getrandbits(32))
        out.append(line)
        size += len(line)
    return ''.join(out).encode()[:n], 'log lines'


def g_fib_literals(r, n):
    # Symbols with Fibonacci frequencies: the Huffman tree is as deep as the
    # alphabet is long, so anything past 16 symbols needs the length limit.
    k = r.randint(16, 40)
    syms = r.sample(range(256), k)
    w = fib_weights(k)
    data = bytes(r.choices(syms, w, k=n))
    return data, 'fibonacci-weighted literals over %d symbols' % k


def g_fib_matches(r, n):
    # Copies whose distances and lengths fall in classes with Fibonacci
    # weights: aims the depth at the distance and length alphabets instead.
    dist_classes = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 32769]
    order = list(range(30))
    r.shuffle(order)
    dw = fib_weights(30)
    len_classes = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                   99, 115, 131, 163, 195, 227, 258, 259]
    lorder = list(range(29))
    r.shuffle(lorder)
    lw = fib_weights(29)
    buf = bytearray(rb(r, min(n, 64)))
    while len(buf) < n:
        if r.random() < 0.3:
            buf += rb(r, r.randint(1, 4))
            continue
        c = order[r.choices(range(30), dw)[0]]
        d = r.randint(dist_classes[c], dist_classes[c + 1] - 1)
        lc = lorder[r.choices(range(29), lw)[0]]
        ln = r.randint(len_classes[lc], max(len_classes[lc], len_classes[lc + 1] - 1))
        if d > len(buf):
            buf += rb(r, min(ln, 8))
            continue
        for _ in range(ln):
            buf.append(buf[-d])
    return bytes(buf[:n]), 'fibonacci-weighted match classes'


def g_runs(r, n):
    # Long runs and exact repeats: matches of 258, distance 1 overlaps, and a
    # repeat at distance exactly 32768 (the window's edge).
    buf = bytearray()
    while len(buf) < n:
        kind = r.randint(0, 3)
        if kind == 0:
            buf += bytes([r.randrange(256)]) * r.choice([1, 2, 3, 257, 258, 259, 516, r.randint(1, 5000)])
        elif kind == 1:
            buf += rb(r, r.randint(1, 300))
        elif kind == 2 and len(buf) >= 32768:
            at = len(buf) - 32768
            buf += buf[at:at + r.randint(3, 300)]
        else:
            buf += rb(r, r.randint(1, 16)) * r.randint(2, 100)
    return bytes(buf[:n]), 'runs and window-edge repeats'


def g_mixed(r, n):
    # Statistics that change mid-stream: segments from the others.
    gens = [g_random, g_text, g_logs, g_fib_literals, g_runs]
    parts, size, names = [], 0, []
    while size < n:
        g = r.choice(gens)
        p, _ = g(r, min(n - size, r.randint(1, max(1, n // 3))))
        parts.append(p)
        size += len(p)
        names.append(g.__name__[2:])
    return b''.join(parts)[:n], 'mixed: ' + ','.join(names[:8])


GENERATORS = [g_random, g_text, g_logs, g_fib_literals, g_fib_matches, g_runs, g_mixed]

BOUNDARY_SIZES = [0, 1, 2, 3, 257, 258, 259, 32767, 32768, 32769, 65535, 65536, 65537,
                  5552, 5553, 131072]


def body_size(r):
    if r.random() < 0.2:
        return r.choice(BOUNDARY_SIZES)
    # Log scale from 1 byte to 2 MB; the far end is where length limiting lives.
    return int(2 ** r.uniform(0, 21))


# ---- libz's streams --------------------------------------------------------

def libz(data, wrap, level, strategy, memlevel):
    c = zlib.compressobj(level, zlib.DEFLATED, WBITS[wrap], memlevel, strategy)
    return c.compress(data) + c.flush()


def libz_inflate(data, wrap):
    """libz's verdict, strictly: the stream must end exactly where the input
    does. Python's zlib.decompress ignores trailing bytes and gzip.decompress
    reads further members; neither is the question asked here."""
    d = zlib.decompressobj(WBITS[wrap])
    try:
        out = d.decompress(data) + d.flush()
    except zlib.error as e:
        return None, str(e)
    if not d.eof:
        return None, 'stream does not end'
    if d.unused_data:
        return None, '%d bytes after the end of the stream' % len(d.unused_data)
    return out, None


STRATEGIES = [('default', zlib.Z_DEFAULT_STRATEGY), ('filtered', zlib.Z_FILTERED),
              ('huffman', zlib.Z_HUFFMAN_ONLY), ('rle', zlib.Z_RLE), ('fixed', zlib.Z_FIXED)]


def mutate(r, s):
    b = bytearray(s)
    kind = r.randint(0, 5)
    if not b:
        return bytes(rb(r, r.randint(1, 8))), 'random bytes'
    i = r.randrange(len(b))
    if kind == 0:
        b[i] ^= 1 << r.randrange(8)
        return bytes(b), 'bit %d flipped' % i
    if kind == 1:
        b[i] = r.randrange(256)
        return bytes(b), 'byte %d replaced' % i
    if kind == 2:
        return bytes(b[:i]), 'truncated to %d' % i
    if kind == 3:
        return bytes(b[:i] + rb(r, r.randint(1, 8)) + b[i:]), 'bytes inserted at %d' % i
    if kind == 4:
        j = r.randrange(len(b))
        i, j = min(i, j), max(i, j)
        return bytes(b[:j] + b[i:j] + b[j:]), 'bytes %d..%d duplicated' % (i, j)
    # Several flips in the block headers, where the code tables are.
    for _ in range(r.randint(2, 6)):
        k = r.randrange(min(len(b), 64))
        b[k] ^= 1 << r.randrange(8)
    return bytes(b), 'several flips near the start'


def gen(d, seed, count):
    r = random.Random(seed)
    have_gzip = subprocess.run(['sh', '-c', 'command -v gzip'], capture_output=True).returncode == 0
    for k in range(count):
        g = r.choice(GENERATORS)
        n = body_size(r)
        data, desc = g(r, n)
        base = os.path.join(d, str(k))
        with open(base + '.in', 'wb') as f:
            f.write(data)
        with open(base + '.desc', 'w') as f:
            f.write('%s, %d bytes\n' % (desc, len(data)))

        streams = []
        combos = [('zlib', 6, 'default', zlib.Z_DEFAULT_STRATEGY, 8)]
        for _ in range(3):
            sname, strat = r.choice(STRATEGIES)
            combos.append((r.choice(list(WBITS)), r.randint(0, 9), sname, strat, r.randint(1, 9)))
        for wrap, level, sname, strat, mem in combos:
            s = libz(data, wrap, level, strat, mem)
            tag = 'libz-%d-%s-m%d' % (level, sname, mem)
            with open('%s.L.%s.%s' % (base, wrap, tag), 'wb') as f:
                f.write(s)
            streams.append((wrap, s))

        # GNU gzip's deflate is not libz's, and `-n` keeps the header
        # deterministic. Its streams carry no FNAME (stdin); `-N` would.
        if have_gzip and len(data) <= 4 << 20 and r.random() < 0.5:
            lvl = r.randint(1, 9)
            s = subprocess.run(['gzip', '-c', '-%d' % lvl], input=data, capture_output=True).stdout
            with open('%s.L.gzip.gnu-%d' % (base, lvl), 'wb') as f:
                f.write(s)

        # Mutants only of the smaller streams: what is being asked is whether
        # the decoder refuses or answers the same as libz, never crashes, and a
        # large body does not ask that any harder.
        if len(data) <= 1 << 16:
            for i in range(r.randint(2, 8)):
                wrap, s = r.choice(streams)
                m, how = mutate(r, s)
                mbase = '%s.M%d.%s' % (base, i, wrap)
                with open(mbase, 'wb') as f:
                    f.write(m)
                with open(mbase + '.how', 'w') as f:
                    f.write(how + '\n')
                out, _ = libz_inflate(m, wrap)
                if out is not None:
                    with open(mbase + '.want', 'wb') as f:
                        f.write(out)


def check(d):
    have_gzip = subprocess.run(['sh', '-c', 'command -v gzip'], capture_output=True).returncode == 0
    n = 0
    for name in sorted(os.listdir(d)):
        parts = name.split('.')
        if len(parts) != 4 or parts[1] != 'O':
            continue
        k, _, wrap, level = parts
        with open(os.path.join(d, k + '.in'), 'rb') as f:
            want = f.read()
        with open(os.path.join(d, name), 'rb') as f:
            s = f.read()
        n += 1
        out, err = libz_inflate(s, wrap)
        if out is None:
            print('FAIL %s: libz refused our stream: %s' % (name, err))
        elif out != want:
            print('FAIL %s: libz inflated our stream to different bytes' % name)
        if wrap == 'gzip' and have_gzip:
            p = subprocess.run(['gzip', '-dc'], input=s, capture_output=True)
            if p.returncode != 0:
                print('FAIL %s: gzip -d refused our stream: %s' % (name, p.stderr.decode(errors='replace').strip()))
            elif p.stdout != want:
                print('FAIL %s: gzip -d inflated our stream to different bytes' % name)
    print('checked %d' % n)


if __name__ == '__main__':
    if sys.argv[1] == 'gen':
        gen(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
    elif sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        sys.exit('usage: zlib-oracle.py gen DIR SEED COUNT | check DIR')
