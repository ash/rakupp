# Regression: the Grand Review, batch C4a — one built-in type lattice
# (docs/dev/findings/REVIEW-GRAND.md). `~~` and `.isa`/`.does`/`.^mro`/isa-ok
# read two tables that had drifted: Bool was an Int for `~~` but not for
# `.isa`, Str was Stringy for `~~` but not for `.does`, and an allomorph was
# an Int but not a Str for `~~`. Every case is what Rakudo answers.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

# 1. Bool is an Int.
check(True.isa(Int),             True,  'True.isa(Int)');
check(Bool.^mro.map(*.^name).List, ('Bool', 'Int', 'Cool', 'Any', 'Mu'), 'Bool.^mro lists Int');
check(Bool ~~ Int,               True,  'Bool ~~ Int');
check(True.does(Int),            True,  'True.does(Int)');

# 2. Str does Stringy — a role, so not an isa ancestor and not in the MRO.
check("a".does(Stringy),         True,  '"a".does(Stringy)');
check("a".isa(Stringy),          False, '"a".isa(Stringy) is False: a role is not a class');
check(Str.^mro.map(*.^name).List, ('Str', 'Cool', 'Any', 'Mu'), 'Str.^mro has no role in it');
check(Str ~~ Stringy,            True,  'Str ~~ Stringy');

# 3. An allomorph is both its number and a Str.
check(IntStr ~~ Int,             True,  'IntStr ~~ Int');
check(IntStr ~~ Str,             True,  'IntStr ~~ Str');
check(<5> ~~ Str,                True,  '<5> ~~ Str');
check(<5>.isa(Str),              True,  '<5>.isa(Str)');
check(IntStr.^mro.map(*.^name).List, ('IntStr', 'Allomorph', 'Str', 'Int', 'Cool', 'Any', 'Mu'), 'IntStr.^mro');
check(RatStr ~~ Rat,             True,  'RatStr ~~ Rat');
check(NumStr ~~ Str,             True,  'NumStr ~~ Str');

# 4. The numeric tower is unchanged.
check(Int.^mro.map(*.^name).List, ('Int', 'Cool', 'Any', 'Mu'), 'Int.^mro');
check(5.isa(Real),               False, '5.isa(Real): a role');
check(5 ~~ Real,                 True,  '5 ~~ Real');
check(FatRat ~~ Rat,             False, 'FatRat is not a Rat');

# 5. isa-ok judges what it claims to (a nested run, so this file stays plain).
sub tap(Str $code) {
    my $p = run $*EXECUTABLE, '-e', "use Test; $code", :out, :err;
    $p.out.slurp(:close).lines.grep(/^ 'ok' | ^ 'not ok' /).map({ .starts-with('ok') }).List
}
check(tap('isa-ok Buf.new(1), Blob; isa-ok True, Int; isa-ok <5>, Str; done-testing'),
      (True, True, True), 'isa-ok: a Buf is a Blob; Bool is an Int; <5> is a Str');
# (`isa-ok Blob.new(1), Buf` is still ok here: the VALUE-level `.does(Buf)` for a
#  buffer answers True for any buffer kind — the `~~` twin says False. Ledger.)

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
