"""The fetching side of t/oracle/unicode.raku: Unicode's own conformance test
files for the version src/unicode_*_gen.cpp is generated from (18.0.0, as in
tools/ucd), kept in a cache outside the repository and pinned by SHA-256.

    python3 -I unicode-oracle.py check DIR
    python3 -I unicode-oracle.py fetch DIR

`check` exits 0 when every file is in DIR with its pinned hash, and names the
missing or different ones otherwise. `fetch` downloads what is missing from
unicode.org (urllib, else curl), refuses anything whose hash is not the
pinned one, and unpacks CollationTest_NON_IGNORABLE.txt from the zip. Nothing
else ever downloads: the gate only calls `fetch` when given --fetch.
"""

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

VERSION = '18.0.0'
PUBLIC = 'https://www.unicode.org/Public/%s/' % VERSION

# name, URL, SHA-256 of the file as published
FILES = [
    ('NormalizationTest.txt', PUBLIC + 'ucd/NormalizationTest.txt',
     '25a50d816764b04abfb4a646d3eb2b2a803284c3873d9a06757b94fe4513dde3'),
    ('GraphemeBreakTest.txt', PUBLIC + 'ucd/auxiliary/GraphemeBreakTest.txt',
     'b0cf047ee94485bbdc846de2b902f5f8a815f6b674f9d04223cddadd91c9df31'),
    ('CollationTest.zip', PUBLIC + 'uca/CollationTest.zip',
     '02af87f32b787fef873eaa9a279420f41c6ea58caa15ed95821e911fc91bf662'),
]

# unpacked from CollationTest.zip: member, SHA-256
UNPACKED = [
    ('CollationTest/CollationTest_NON_IGNORABLE.txt',
     '86127b4008ab6f0d275d1a779bc3148363b4153388f1b65058074323f41b58fb'),
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def problems(d):
    out = []
    for name, _, want in FILES:
        p = os.path.join(d, name)
        if not os.path.exists(p):
            out.append('%s: missing' % name)
        elif sha256(p) != want:
            out.append('%s: not the pinned file (SHA-256 %s)' % (name, sha256(p)))
    for member, want in UNPACKED:
        p = os.path.join(d, os.path.basename(member))
        if not os.path.exists(p):
            out.append('%s: missing' % os.path.basename(member))
        elif sha256(p) != want:
            out.append('%s: not the pinned file (SHA-256 %s)' % (os.path.basename(member), sha256(p)))
    return out


def download(url, dest):
    try:
        with urllib.request.urlopen(url, timeout=120) as r, open(dest, 'wb') as f:
            shutil.copyfileobj(r, f)
        return
    except Exception as e:                     # an old Python without CA roots, say
        err = e
    if shutil.which('curl'):
        if subprocess.call(['curl', '-sSfL', '-o', dest, url]) == 0:
            return
    sys.exit('cannot download %s: %s' % (url, err))


def fetch(d):
    os.makedirs(d, exist_ok=True)
    for name, url, want in FILES:
        p = os.path.join(d, name)
        if os.path.exists(p) and sha256(p) == want:
            continue
        fd, tmp = tempfile.mkstemp(dir=d)
        os.close(fd)
        print('fetching %s' % url)
        download(url, tmp)
        got = sha256(tmp)
        if got != want:
            os.unlink(tmp)
            sys.exit('%s: SHA-256 %s, pinned %s; not kept' % (url, got, want))
        os.replace(tmp, p)
        os.chmod(p, 0o644)
    with zipfile.ZipFile(os.path.join(d, 'CollationTest.zip')) as z:
        for member, want in UNPACKED:
            p = os.path.join(d, os.path.basename(member))
            if os.path.exists(p) and sha256(p) == want:
                continue
            with z.open(member) as src, open(p, 'wb') as dst:
                shutil.copyfileobj(src, dst)
            if sha256(p) != want:
                sys.exit('%s: unpacked, but not the pinned file' % member)


# ---- props: what the vendored UCD files say, re-read --------------------------
#
#     python3 -I unicode-oracle.py props UCD_DIR OUT
#
# writes, for every property below and every code point, the value the UCD
# text gives, as runs. This reads the files itself and shares no code with
# tools/gen_unicode_*: a different reading of the same text is the oracle.
# Defaults are the files' own `@missing` lines. Format, one record a line:
#
#     P <property> <kind>       kind: enum, bool, cps (a string as code
#                               points, `-` when empty, `=` for the code
#                               point itself), name (`%` in it stands for
#                               the code point as %04X), rat
#     <lo> <hi> <value>         hex range, the value for each code point in it
#     A <cp> <alias>            a NameAliases alias, for uniparse
#
# Properties Raku++ does not expose are not written; t/oracle/unicode.raku
# names them.

MAX = 0x10FFFF


def ucd_lines(path):
    with open(path, encoding='utf-8') as f:
        for line in f:
            yield line.rstrip('\n')


def ucd_range(field):
    field = field.strip()
    if '..' in field:
        lo, hi = field.split('..')
        return int(lo, 16), int(hi, 16)
    return int(field, 16), int(field, 16)


# @missing values are long aliases where data lines use short ones
MISSING_ALIAS = {'Left_To_Right': 'L', 'Right_To_Left': 'R', 'Arabic_Letter': 'AL',
                 'European_Terminator': 'ET', 'Not_Applicable': 'NA'}


class Table:
    """A value per code point, built from defaults and then data lines."""
    def __init__(self, default):
        self.v = [default] * (MAX + 1)

    def set(self, lo, hi, val):
        self.v[lo:hi + 1] = [val] * (hi - lo + 1)

    def runs(self):
        v, lo = self.v, 0
        for cp in range(1, MAX + 2):
            if cp > MAX or v[cp] != v[lo]:
                yield lo, cp - 1, v[lo]
                lo = cp


def enum_file(path, prop=None, field=1):
    """A file of `range ; value` (or `range ; prop ; value` when prop is
    given), its @missing lines first in file order, then its data."""
    t = None
    data = []
    for line in ucd_lines(path):
        if line.startswith('# @missing:'):
            parts = [p.strip() for p in line[len('# @missing:'):].split(';')]
            if prop is not None:
                if parts[1] != prop:
                    continue
                val = parts[2]
            else:
                val = parts[1]
            val = MISSING_ALIAS.get(val, val)
            lo, hi = ucd_range(parts[0])
            if t is None:
                t = Table(val)
            t.set(lo, hi, val)
            continue
        line = line.split('#')[0].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split(';')]
        if prop is not None:
            if len(parts) < 3 or parts[1] != prop:
                continue
            data.append((ucd_range(parts[0]), parts[2]))
        else:
            data.append((ucd_range(parts[0]), parts[field]))
    if t is None:
        sys.exit('%s: no @missing line%s' % (path, ' for ' + prop if prop else ''))
    for (lo, hi), val in data:
        t.set(lo, hi, val)
    return t


def binary_props(path):
    """name -> set of code points, for every `range ; Name` line (two fields)."""
    out = {}
    for line in ucd_lines(path):
        line = line.split('#')[0].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split(';')]
        if len(parts) != 2:
            continue
        lo, hi = ucd_range(parts[0])
        out.setdefault(parts[1], []).append((lo, hi))
    return out


def unicode_data(path):
    """code point -> fields, with First/Last ranges expanded to (lo, hi, fields)."""
    single, ranges, first = {}, [], None
    for line in ucd_lines(path):
        if not line:
            continue
        f = line.split(';')
        cp = int(f[0], 16)
        if f[1].endswith(', First>'):
            first = (cp, f)
        elif f[1].endswith(', Last>'):
            ranges.append((first[0], cp, first[1]))
        else:
            single[cp] = f
    return single, ranges


CCC_NAMES = {0: 'Not_Reordered', 1: 'Overlay', 6: 'Han_Reading', 7: 'Nukta', 8: 'Kana_Voicing',
             9: 'Virama', 200: 'Attached_Below_Left', 202: 'Attached_Below', 214: 'Attached_Above',
             216: 'Attached_Above_Right', 218: 'Below_Left', 220: 'Below', 222: 'Below_Right',
             224: 'Left', 226: 'Right', 228: 'Above_Left', 230: 'Above', 232: 'Above_Right',
             233: 'Double_Below', 234: 'Double_Above', 240: 'Iota_Subscript'}


def ccc_name(n):
    # 10..199 have only the aliases CCC10..CCC199; Raku (both engines) answers
    # the number for those
    return CCC_NAMES.get(n, str(n))


def hexes(cps):
    return ' '.join('%04X' % c for c in cps) if cps else '-'


def props(ucd, out):
    def f(name):
        return os.path.join(ucd, '%s-%s.txt' % (name, VERSION))

    w = open(out, 'w', encoding='utf-8')

    def emit(prop, kind, table):
        w.write('P %s %s\n' % (prop, kind))
        for lo, hi, val in table.runs():
            w.write('%04X %04X %s\n' % (lo, hi, val))

    single, ranges = unicode_data(f('UnicodeData'))

    def each_ud():                                 # (lo, hi, fields)
        for cp, fl in single.items():
            yield cp, cp, fl
        for r in ranges:
            yield r

    # -- UnicodeData's own fields
    gc = Table('Cn')
    ccc = Table(ccc_name(0))
    mirrored = Table('False')
    for lo, hi, fl in each_ud():
        gc.set(lo, hi, fl[2])
        ccc.set(lo, hi, ccc_name(int(fl[3])))
        mirrored.set(lo, hi, 'True' if fl[9] == 'Y' else 'False')
    emit('General_Category', 'enum', gc)
    emit('Canonical_Combining_Class', 'enum', ccc)
    emit('Bidi_Mirrored', 'bool', mirrored)

    # -- one enumerated property a file
    for prop, name in [('Age', 'DerivedAge'), ('Bidi_Class', 'DerivedBidiClass'),
                       ('Script', 'Scripts'), ('Block', 'Blocks'),
                       ('East_Asian_Width', 'EastAsianWidth'), ('Line_Break', 'LineBreak'),
                       ('Word_Break', 'WordBreakProperty'), ('Sentence_Break', 'SentenceBreakProperty'),
                       ('Grapheme_Cluster_Break', 'GraphemeBreakProperty'),
                       ('Hangul_Syllable_Type', 'HangulSyllableType'),
                       ('Numeric_Type', 'DerivedNumericType'),
                       ('Decomposition_Type', 'DerivedDecompositionType'),
                       ('Indic_Syllabic_Category', 'IndicSyllabicCategory'),
                       ('Indic_Positional_Category', 'IndicPositionalCategory')]:
        emit(prop, 'enum', enum_file(f(name)))

    # -- Joining_Group as Raku spells it: ArabicShaping's form, which roast
    #    pins ("SYRIAC WAW"), not DerivedJoiningGroup's Syriac_Waw
    jg = enum_file(f('DerivedJoiningGroup'))
    jg.v = [v if v == 'No_Joining_Group' else v.upper().replace('_', ' ') for v in jg.v]
    emit('Joining_Group', 'enum', jg)

    # -- enumerated properties inside multi-property files
    qc_words = {'Y': 'Yes', 'N': 'No', 'M': 'Maybe', 'Yes': 'Yes', 'No': 'No', 'Maybe': 'Maybe'}
    for form in ('NFD', 'NFC', 'NFKD', 'NFKC'):
        t = enum_file(f('DerivedNormalizationProps'), prop=form + '_QC')
        t.v = [qc_words[x] for x in t.v]
        emit(form + '_Quick_Check', 'enum', t)
    emit('Indic_Conjunct_Break', 'enum', enum_file(f('DerivedCoreProperties'), prop='InCB'))

    # -- Joining_Type: ArabicShaping lists the joining ones; of the rest, Mn,
    #    Me and Cf are Transparent and everything else Non_Joining (its header)
    jt = Table('U')
    for cp in range(MAX + 1):
        if gc.v[cp] in ('Mn', 'Me', 'Cf'):
            jt.v[cp] = 'T'
    for line in ucd_lines(f('ArabicShaping')):
        line = line.split('#')[0].strip()
        if line:
            parts = [p.strip() for p in line.split(';')]
            jt.v[int(parts[0], 16)] = parts[2]
    emit('Joining_Type', 'enum', jt)

    # -- bidi brackets and mirroring
    bpt = Table('n')
    for line in ucd_lines(f('BidiBrackets')):
        line = line.split('#')[0].strip()
        if line:
            parts = [p.strip() for p in line.split(';')]
            bpt.v[int(parts[0], 16)] = parts[2]
    emit('Bidi_Paired_Bracket_Type', 'enum', bpt)
    bmg = Table('-')
    for line in ucd_lines(f('BidiMirroring')):
        line = line.split('#')[0].strip()
        if line:
            parts = [p.strip() for p in line.split(';')]
            bmg.v[int(parts[0], 16)] = parts[1]
    emit('Bidi_Mirroring_Glyph', 'cps', bmg)

    # -- binary properties: every one the files define
    for name in ('PropList', 'DerivedCoreProperties', 'emoji-data', 'DerivedNormalizationProps'):
        for prop, rs in sorted(binary_props(f(name)).items()):
            if prop.startswith('Expands_On_'):         # deprecated since 6.0, and not exposed
                continue
            t = Table('False')
            for lo, hi in rs:
                t.set(lo, hi, 'True')
            emit(prop, 'bool', t)

    # -- numeric value, as an exact fraction
    nv = Table('NaN')
    for line in ucd_lines(f('DerivedNumericValues')):
        line = line.split('#')[0].strip()
        if line:
            parts = [p.strip() for p in line.split(';')]
            lo, hi = ucd_range(parts[0])
            nv.set(lo, hi, parts[3])
    emit('Numeric_Value', 'rat', nv)

    # -- case: simple mappings from UnicodeData (a code point maps to itself
    #    unless it says otherwise), full ones from SpecialCasing's
    #    unconditional lines over them, folding from CaseFolding
    simple = {}
    for i, prop in ((12, 'Simple_Uppercase_Mapping'), (13, 'Simple_Lowercase_Mapping'),
                    (14, 'Simple_Titlecase_Mapping')):
        t = Table(None)
        t.v = ['='] * (MAX + 1)                       # = : the code point itself
        for cp, fl in single.items():
            if fl[i]:
                t.v[cp] = fl[i]
        simple[prop] = t
        emit(prop, 'cps', t)
    special = {}
    for line in ucd_lines(f('SpecialCasing')):
        line = line.split('#')[0].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split(';')]
        if len(parts) > 4 and parts[4]:              # a condition: not the default mapping
            continue
        special[int(parts[0], 16)] = parts[1:4]      # lower, title, upper
    for prop, s, k in (('Lowercase_Mapping', 'Simple_Lowercase_Mapping', 0),
                       ('Titlecase_Mapping', 'Simple_Titlecase_Mapping', 1),
                       ('Uppercase_Mapping', 'Simple_Uppercase_Mapping', 2)):
        t = Table(None)
        t.v = list(simple[s].v)
        for cp, m in special.items():
            t.v[cp] = m[k] or '-'
        emit(prop, 'cps', t)
    cf_full, cf_simple = Table(None), Table(None)
    cf_full.v = ['='] * (MAX + 1)
    cf_simple.v = list(cf_full.v)
    for line in ucd_lines(f('CaseFolding')):
        line = line.split('#')[0].strip()
        if not line:
            continue
        code, status, mapping = [p.strip() for p in line.split(';')[:3]]
        cp = int(code, 16)
        if status in ('C', 'F'):
            cf_full.v[cp] = mapping
        if status in ('C', 'S'):
            cf_simple.v[cp] = mapping
    emit('Case_Folding', 'cps', cf_full)
    emit('Simple_Case_Folding', 'cps', cf_simple)

    # -- names, as `.uniname` gives them: the Name property, or a label in
    #    angle brackets where it is empty (UAX #44 / Unicode 4.8)
    name = Table(None)
    for cp in range(MAX + 1):
        name.v[cp] = '<reserved-%>'                 # % : the code point, as %04X
    for cp in range(MAX + 1):
        if (cp & 0xFFFE) == 0xFFFE or 0xFDD0 <= cp <= 0xFDEF:
            name.v[cp] = '<noncharacter-%>'
    named = []                                       # (lo, hi, prefix): Scripts.txt ranges whose
    for line in ucd_lines(f('Scripts')):             # comment names its first as PREFIX-<hex>
        if '..' in line and '#' in line and not line.startswith('#'):
            lo, hi = ucd_range(line.split(';')[0])
            m = line.split('#', 1)[1].split()
            first = ' '.join(m[2:]).split('..')[0] if len(m) > 2 else ''
            suffix = '-%X' % lo
            if first.endswith(suffix):
                named.append((lo, hi, first[:-len(suffix)] + '-'))
    named.sort()
    jamo = {}
    for line in ucd_lines(f('Jamo')):
        line = line.split('#')[0].strip()
        if line:
            code, short = [p.strip() for p in line.split(';')]
            jamo[int(code, 16)] = short
    for cp, fl in single.items():
        n = fl[1]
        if n == '<control>':
            name.v[cp] = '<control-%>'
        else:
            name.v[cp] = n
    for lo, hi, fl in ranges:
        label = fl[1]
        covering = [r for r in named if r[0] <= hi and r[1] >= lo]
        for cp in range(lo, hi + 1):
            if 'Surrogate' in label:
                name.v[cp] = '<surrogate-%>'
            elif 'Private Use' in label:
                name.v[cp] = '<private-use-%>'
            elif 'Hangul Syllable' in label:
                s = cp - 0xAC00                          # Unicode 3.12, Hangul syllable names
                l, v, t = 0x1100 + s // 588, 0x1161 + (s % 588) // 28, 0x11A7 + s % 28
                name.v[cp] = 'HANGUL SYLLABLE ' + jamo[l] + jamo[v] + (jamo[t] if t != 0x11A7 else '')
            else:
                # the prefix Scripts.txt's comment gives the range covering cp
                p = [r[2] for r in covering if r[0] <= cp <= r[1]]
                if not p:
                    sys.exit('no name prefix for %s at %04X' % (label, cp))
                name.v[cp] = p[0] + '%'                  # all past U+3400: %X is %04X
    emit('uniname', 'name', name)
    u1 = Table('-')
    for cp, fl in single.items():
        if fl[10]:
            u1.v[cp] = fl[10]
    emit('Unicode_1_Name', 'name', u1)

    # -- every alias, for uniparse: `A <cp> <alias>`
    for line in ucd_lines(f('NameAliases')):
        line = line.split('#')[0].strip()
        if line:
            code, alias, kind = line.split(';')
            w.write('A %s %s\n' % (code, alias))
    w.close()


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'props':
        props(sys.argv[2], sys.argv[3])
        sys.exit(0)
    if len(sys.argv) == 3 and sys.argv[1] == 'check':
        bad = problems(sys.argv[2])
        for b in bad:
            print(b)
        sys.exit(1 if bad else 0)
    if len(sys.argv) == 3 and sys.argv[1] == 'fetch':
        fetch(sys.argv[2])
        bad = problems(sys.argv[2])
        for b in bad:
            print(b)
        sys.exit(1 if bad else 0)
    sys.exit('usage: unicode-oracle.py check|fetch DIR')
