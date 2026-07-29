# Reported by the Cognates port (docs/rakupp-findings finding 1): Raku indices are
# GRAPHEME indices, but substr/index/flip/substr-rw walked CODEPOINTS. The two
# agree until a combining mark appears, and then nothing raises an error — the
# answer is simply wrong. A JSON scanner using `$src.substr($i, 1)` against a
# `$len = $src.chars` bound ran off the end of every file containing "h₂ŕ̥tḱos".
#
# Also fixed here: regex .from/.to reported BYTE offsets, so they were wrong for
# any non-ASCII string at all, not just clustered ones.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $s = "ŕ̥tḱos";                 # 5 graphemes, 6 codepoints
check($s.chars,       5,      'chars counts graphemes');
check($s.ords.elems,  6,      'ords counts codepoints');
check($s.substr(1, 1), 't',   'substr indexes graphemes, not codepoints');
check($s.substr(0, 2), "ŕ̥t",  'a cut lands on a cluster boundary');
check($s.substr(1..2), 'tḱ',  'the Range form too');
check($s.index('t'),   1,     'index reports a grapheme position');
check($s.index('os'),  3,     'and for a multi-char needle');
check($s.rindex('os'), 3,     'rindex likewise');
check($s.flip,        'soḱtŕ̥', 'flip reverses clusters, keeping marks on their base');

my $r = $s;
$r.substr-rw(1, 1) = 'X';
check($r, "ŕ̥Xḱos", 'substr-rw splices by grapheme');

# A lone combining mark is its own grapheme and must NOT match inside a cluster.
check($s.index("\c[COMBINING RING BELOW]"), Nil, 'no match in the middle of a cluster');

# .from/.to are grapheme offsets — this was byte offsets, so plain accented
# text was already wrong before any clustering came into it.
check(("áb" ~~ /b/).from, 1, 'match .from is a grapheme offset, not a byte one');
check(($s  ~~ /t/).from,  1, 'and is unaffected by a preceding combining mark');
check(($s  ~~ /t/).to,    2, '.to likewise');

# The pure-ASCII fast path must still agree with the general one.
check('hello'.substr(1, 3), 'ell', 'ASCII substr');
check('hello'.index('llo'), 2,     'ASCII index');
check('hello'.flip,        'olleh', 'ASCII flip');
check("a\r\nb".chars,       3,      'CRLF is one grapheme');
check("a\r\nb".substr(1, 1), "\r\n", 'and substr treats it as one');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
