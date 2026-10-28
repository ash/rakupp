# Regression: a module's own `sub q` does not reach the file that uses it
# (issue #194).
#
# A routine named like a quote word makes `q{…}` a CALL wherever the routine
# is in scope — Rakudo reads it that way, and so does rakupp. Which names an
# imported module brings in is decided by reading its source before this file
# is parsed, and that read took every `sub q` it found, so a module's lexical
# `sub q` turned `q{single quoted}` here into "Undefined routine 'q'", and a
# `sub s` two modules down disabled `s///`. Only what the import can bring in
# counts now: a sub marked `is export`, or any of them when the module has an
# `EXPORT` sub. Every answer below is Rakudo 2026.09's.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppQuoteSubPrivate;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck q{single quoted}, 'single quoted', 'q{} is a quote here';
my $s = 'abc';
ck qq{a $s b}, 'a abc b', 'qq{} too, from a module two levels down';
ck ($s ~~ s/b/B/) ?? $s !! 'no', 'aBc', 's/// is a substitution';
my $t = 'abc';
ck ($t ~~ tr/a/z/) ?? $t !! 'no', 'zbc', 'tr/// is a transliteration';
ck private-q-works(), '<in>[inner]', 'the modules still call their own';

say 'PASS';
