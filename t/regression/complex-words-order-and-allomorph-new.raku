# BROKE: three places where Raku++ read Complex values and allomorphs more
# loosely than Rakudo, found 2026-10-23 by FatRoast's strings-v2, which keeps
# a word-list word that is a number and so ran these on both engines:
#   - a word-list word took any spelling of Inf or NaN, so `<0+NaNi>` was a
#     Complex where Rakudo, as its val() does, keeps a Str (the imaginary
#     part needs `\i` to be Inf or NaN, and a NaN one stands alone)
#   - a Complex against a plain Str ordered by number: `"abc" cmp <0+0i>`
#     was Same, `"0" cmp 0+0i` Same; Rakudo compares the strings, as it
#     does for an Int, and `1 before <1+0i>` was True beside a Same `cmp`
#   - IntStr.new, RatStr.new, NumStr.new and ComplexStr.new bound anything:
#     `IntStr.new("x", "y")` made an IntStr of two Strs, where Rakudo dies
#     binding `Int:D $i` (S32-str/val.t, fattened, died on Rakudo there)
# FIXED: angleWordNumeric (src/Parser.cpp) takes a digitless part only as
# Inf or NaN, as valAllomorph does; the Complex branch of applyArith
# (src/InterpreterCore.cpp) orders a Complex and a plain Str as strings and
# answers `before`/`after` from its own `cmp`; the allomorph constructors
# (src/MethodCallPart1c.cpp) check `(Int:D $i, Str:D $s)` and the others.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# word-list words, as val() reads them
ck(<0+NaNi>.^name, 'Str', '<0+NaNi>');
ck(<NaNi>.^name, 'Str', '<NaNi>');
ck(<-NaN\i>.^name, 'Str', '<-NaN\i>');
ck(<Infi>.^name, 'Str', '<Infi>');
ck(<inf+1i>.^name, 'Str', '<inf+1i>');
ck(<NaN\i>.^name, 'ComplexStr', '<NaN\i>');
ck(<-Inf\i>.^name, 'ComplexStr', '<-Inf\i>');
ck(<NaN+Inf\i>.^name, 'Complex', '<NaN+Inf\i>');
ck(<NaN+1i>.^name, 'Complex', '<NaN+1i>');
ck(<a 0+NaNi 1+2i>.map(*.^name).List, <Str Str ComplexStr>, 'in a list');

# a Complex and a plain Str order as strings
ck("abc" cmp <0+0i>, More, '"abc" cmp <0+0i>');
ck(<0+0i> cmp "abc", Less, '<0+0i> cmp "abc"');
ck("0" cmp 0+0i, Less, '"0" cmp 0+0i');
ck("abc" before <0+0i>, False, 'before');
ck(1+0i cmp <0+0i>, More, 'two Complex still by value');
ck(1 cmp <1+0i>, Same, '1 cmp <1+0i>');
ck(1 before <1+0i>, False, '…and so before');

# the allomorph constructors check what they bind
for ｢IntStr.new("x", "y")｣, ｢RatStr.new(1, "a")｣, ｢NumStr.new(1, "a")｣, ｢ComplexStr.new("x", "a")｣,
    ｢IntStr.new(1, 2)｣, ｢RatStr.new(FatRat.new(1, 2), "a")｣ -> $code {
    ck((try EVAL $code) // $!.^name, 'X::TypeCheck::Binding::Parameter', $code);
}
ck((try IntStr.new(Int, "a")) // $!.^name, 'X::Parameter::InvalidConcreteness', 'IntStr.new(Int, …)');
ck(IntStr.new(1, <2>).raku, 'IntStr.new(1, "2")', 'an allomorph is a Str');
ck(IntStr.new(True, "a").raku, 'IntStr.new(1, "a")', 'a Bool binds as its Int');
ck(RatStr.new(1/2, "a").raku, 'RatStr.new(0.5, "a")', 'RatStr.new(Rat, …)');

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
