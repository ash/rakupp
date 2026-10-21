# Regression: `uc`, `lc`, `tc`, `chars` and `ord` as subs are the method on
# their argument.
#
# The sub forms went straight to the string code with the argument's string
# form, so `uc("a"|"b")` was "ANY" (the junction's name, upper-cased),
# `chars("ab"|"c")` 3, `ord("a"|"b")` 97, and `uc($obj)` ignored the object's
# own `.uc` and upper-cased its default Str. `flip` and `ords` already
# delegated. `--exe -O` calls the same functions directly (Codegen's fastB),
# so it had the same answers. A plain Str or Int still takes the short path.
# Found by the 2026-10-21 review (O4). Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- a Junction autothreads ----
ck uc("a"|"b").raku,    'any("A", "B")', 'uc of a junction';
ck lc("A"|"B").raku,    'any("a", "b")', 'lc of a junction';
ck tc("a"|"b").raku,    'any("A", "B")', 'tc of a junction';
ck chars("ab"|"c").raku, 'any(2, 1)',    'chars of a junction';
ck ord("a"|"b").raku,    'any(97, 98)',  'ord of a junction';
ck so(chars("ab"|"c") == 1), True,       '…and it compares as one';
ck uc(all("x", "y")).raku, 'all("X", "Y")', 'an all() junction';

# ---- an object answers with its own method ----
class C {
    method uc    { "UC!" }
    method lc    { "lc!" }
    method tc    { "Tc!" }
    method chars { 42 }
    method ord   { 7 }
}
my $c = C.new;
ck uc($c),    $c.uc,    'uc($obj) is $obj.uc';
ck lc($c),    $c.lc,    'lc($obj) is $obj.lc';
ck tc($c),    $c.tc,    'tc($obj) is $obj.tc';
ck chars($c), $c.chars, 'chars($obj) is $obj.chars';
ck ord($c),   $c.ord,   'ord($obj) is $obj.ord';

# ---- a Buf is not a Str ----
ck (try chars(Buf.new(1, 2, 3))) // 'died', 'died', 'chars(Buf) refuses, as .chars does';

# ---- the plain cases, and the method form beside the sub ----
for "straße", "ÀB", "q\x[301]", "a\r\nb", "\x[1F44D]\x[1F3FD]", "ǆemal", "" -> $s {
    ck uc($s), $s.uc, "uc {$s.raku}";
    ck lc($s), $s.lc, "lc {$s.raku}";
    ck tc($s), $s.tc, "tc {$s.raku}";
    ck chars($s), $s.chars, "chars {$s.raku}";
    ck ord($s), $s.ord, "ord {$s.raku}" if $s;
}
ck (uc(42), chars(12345), ord(65)), ("42", 5, 54), 'an Int';
ck (uc(<ab>), chars(<12>), ord(<5>)), ("AB", 2, 53), 'allomorphs';
ck (uc(True), chars(1.5)), ("TRUE", 3), 'a Bool and a Rat, through their methods';

say $fails ?? "FAIL ($fails)" !! 'PASS';
