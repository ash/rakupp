# Regression: an imported operator lexes whole, as the file's own declaration
# of it does (issue #60).
#
# The whole unit is lexed before the parser reaches a `use`, and the lexer
# learns the operator spellings to read whole only from the file's own
# declarations. The parser registered a module's exported operators, but the
# lexer never heard of them, so a spelling the built-in rules split stayed
# split: an imported `(<&)` was `(` and the word list `<&) …>`, `(+&)` was
# `(`, `+&`, `)`, `⨁⨁` two `⨁`s, `√√4` two `√`s, and `1 +++ 2` read as
# `1++ + 2` ("Target is not assignable"). A circumfix `[[ ]]` gave the plain
# array `[6]`, silently. FunctionalParsers exports `(<&)` and `⨁⨁`. Now a
# `use` that brings such a spelling in re-lexes the rest of the unit knowing
# it, the path an imported `sub tr` already took. Every answer below is
# Rakudo 2026.09's, except the last block, which is rakupp's own (see there).
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppImportedOps;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (1 (<&) 2), 'L(1,2)', 'an imported `(<&)`, not a word list';
ck (1 (+&) 2), 'P(1,2)', 'an imported `(+&)`';
ck (1 ⨁⨁ 2), 'OO(1,2)', 'an imported `⨁⨁`, not two `⨁`s';
ck (1 ⨁ 2 ⨁⨁ 3), 'OO(O(1,2),3)', '…beside the imported `⨁`';
ck (1 +++ 2), 'T(1,2)', 'an imported `+++`, not `++` and `+`';
ck (√√4), 'R(4)', 'an imported prefix';
ck (5‼‼), 'F(5)', 'an imported postfix';
ck [[6]], 'C(6)', 'an imported circumfix, not an array';
ck ℵ₀ℵ₀, 'aleph', 'an imported term';
ck tr(7), 'tr(7)', 'the imported `sub tr` still beats the quote form';

# the brackets nearby keep their meanings
ck <a b>.List, ('a', 'b'), 'a word list';
ck (1 < 2 > 0), True, 'a chain of comparisons';
ck (3 +& 6), 2, 'the built-in `+&`';

# An L10N rewrite ran on the first token stream, so the re-lex keeps the words
# it translated. Rakupp's own: the fixture's EXPORT is not a slang Rakudo can
# install (see t/regression/rakuast-l10n.raku), so Rakudo cannot run it.
if $*RAKU.compiler.name eq 'Raku++' {
    my $out = EVAL q:to/RX/;
        use L10N::RX;
        use RakuppImportedOps;
        my $r;
        wenn 0 { $r = 'no' } sonst { $r = 1 (+&) 2 }
        $r
        RX
    ck $out, 'P(1,2)', 'a translated keyword after the re-lex';
}

say 'PASS';
