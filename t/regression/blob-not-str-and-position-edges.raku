# Regression: a Blob has no Str methods, and three edges of the character
# positions fixed by the 2026-10-21 review (O3).
#
#   * `Buf.new(1).uc` answered "\x[1]": a Blob's bytes went to the Str arms as
#     text, and so `.lc`, `.flip`, `.ord`, `.substr`, `.trim`, `.contains`,
#     `.lines` and the rest of what Rakudo's Str has and its Blob does not.
#     Rakudo has no such method; `uc($buf)`, the sub being the method, dies
#     the same way.
#   * `"abc".indices("", 5)` was (3): a start past the end was taken as the
#     end. Rakudo searches from there and finds nothing.
#   * `"abc".contains(/$/, 3)` was True. Rakudo looks for a regex only from a
#     character of the string, so a position at its end is False.
#   * `"abc".split('', 1)` was a Seq. With one Str needle and a limit, Rakudo
#     answers a List where there is nothing to split: the string for a limit
#     of 1, and ("",) or () for an empty string.
#
# Found while fixing O2–O4 (ledger O23). Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub died(&code) { my $r = try code(); $! ?? $!.^name !! "answered {$r.raku}" }

# ---- a Blob is not a Str ----
for <uc lc tc fc tclc flip ord ords chomp chop trim wordcase lines words comb NFC encode> -> $m {
    ck died({ Buf.new(65, 66)."$m"() }), 'X::Method::NotFound', "Buf.$m";
}
ck died({ Buf.new(65).substr(0) }),      'X::Method::NotFound', 'Buf.substr';
ck died({ Buf.new(65).contains("A") }),  'X::Method::NotFound', 'Buf.contains';
ck died({ Buf.new(65).index("A") }),     'X::Method::NotFound', 'Buf.index';
ck died({ Buf.new(65).starts-with("A") }), 'X::Method::NotFound', 'Buf.starts-with';
ck died({ Buf.new(65).subst("A", "b") }), 'X::Method::NotFound', 'Buf.subst';
ck died({ Blob.new(65).uc }),            'X::Method::NotFound', 'Blob.uc';
ck died({ "a".encode.uc }),              'X::Method::NotFound', 'utf8.uc';
ck died({ Buf[uint16].new(65).lc }),     'X::Method::NotFound', 'Buf[uint16].lc';
ck died({ uc(Buf.new(1)) }),             'X::Method::NotFound', 'uc($buf)';
{
    my $b = Buf.new(65);
    try $b.uc;
    ck $!.message, "No such method 'uc' for invocant of type 'Buf'", 'the message names the type';
}
# …what a Blob does have still answers
ck Buf.new(65, 66).list.List, (65, 66), 'Buf.list';
ck Buf.new(65, 66).decode, "AB", 'Buf.decode';
ck Buf.new(65, 66).elems, 2, 'Buf.elems';
ck +Buf.new(65, 66), 2, 'Buf.Numeric';
ck Buf.new(1, 2).reverse.list.List, (2, 1), 'Buf.reverse';

# ---- .indices from past the end ----
ck "abc".indices("", 5), (), '.indices("", 5) past the end';
ck "abc".indices("", 4), (), '.indices("", 4)';
ck "abc".indices("", 3), (3,), '.indices("", 3) at the end';
ck "abc".indices("", 2), (2, 3), '.indices("", 2)';
ck "abc".indices("b", 5), (), '.indices("b", 5)';
ck "q\x[301]bc".indices("", 3), (3,), 'positions are characters';

# ---- .contains(/rx/, $pos) only from a character of the string ----
ck "abc".contains(/$/, 3), False, '.contains(/$/, 3)';
ck "abc".contains(/<?>/, 3), False, '.contains(/<?>/, 3)';
ck "".contains(/$/, 0), False, '"".contains(/$/, 0)';
ck "abc".contains(/c/, 4), False, '.contains(/c/, 4)';
ck "abc".contains(/$/, 2), True, '.contains(/$/, 2)';
ck "abc".contains(/c/, 2), True, '.contains(/c/, 2)';
ck "abc".contains(/$/), True, '.contains(/$/) with no position';
ck "".contains(/$/), True, '"".contains(/$/)';
ck "abc".contains("", 3), True, 'a Str needle at the end is still found';

# ---- .split(Str, $limit): a List where there is nothing to split ----
sub kind($x) { ($x.^name, $x.List) }
ck kind("abc".split('', 1)),     ('List', ("abc",)), ".split('', 1)";
ck kind("abc".split('b', 1)),    ('List', ("abc",)), ".split('b', 1)";
ck kind("abc".split('b', 1, :v)), ('List', ("abc",)), ".split('b', 1, :v)";
ck kind("abc".split('b', 1, :skip-empty)), ('List', ("abc",)), ".split('b', 1, :skip-empty)";
ck kind("".split('b', 2)),       ('List', ("",)), "''.split('b', 2)";
ck kind("".split('b', *)),       ('List', ("",)), "''.split('b', *)";
ck kind("".split('b', 1, :skip-empty)), ('List', ("",)), "''.split('b', 1, :skip-empty)";
ck kind("".split('', 1)),        ('List', ()), "''.split('', 1)";
ck kind("".split('', Inf)),      ('List', ()), "''.split('', Inf)";
ck kind(12.split(2, 1)),         ('List', ("12",)), "12.split(2, 1)";
# …and a Seq everywhere else
ck kind("abc".split('', 2)),     ('Seq', ("", "abc")), ".split('', 2)";
ck kind("abc".split('b', 2)),    ('Seq', ("a", "c")), ".split('b', 2)";
ck kind("abc".split('b', 0)),    ('Seq', ()), ".split('b', 0)";
ck kind("abc".split('b')),       ('Seq', ("a", "c")), ".split('b')";
ck kind("".split('b')),          ('Seq', ("",)), "''.split('b')";
ck kind("abc".split(/b/, 1)),    ('Seq', ("abc",)), ".split(/b/, 1)";
ck kind("abc".split(('b',), 1)), ('Seq', ("abc",)), ".split(('b',), 1)";
ck kind("abc".split('b', 1, :end)), ('Seq', ("abc",)), ".split('b', 1, :end)";
ck kind("abc".split('', 1, :end)), ('Seq', ("abc",)), ".split('', 1, :end)";

say $fails ?? "FAIL ($fails)" !! 'PASS';
