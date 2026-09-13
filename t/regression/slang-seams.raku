# Regression: slangs (docs/dev/plans/SLANG-PLAN.md). A `use Slang::X` runs the
# module for real in a scratch interpreter with a compile-time $*LANG; what its
# roles override becomes SEAMS — the slang's own token runs where the built-in
# lexer would start one, and its action's result is what parses — and, for the
# two Tuxic productions whose bodies call Rakudo's grammar, MODES. The fixture
# registers through $*LANG.define_slang directly and carries both role flavours,
# so this file runs under Rakudo too (its legacy grammar takes the other role).
#
#   * number:sym<X> / value:sym<X>: the token runs, its action's literal parses
#   * a `<(` / `)>` in the entry token: the capture is trimmed, matching is not
#   * identifier: `pass?` names a routine
#   * sigilless-variable: `my 👍 = 42`, and `👍 => v` keys on its VALUE
#   * pointy-block-starter: λ opens a pointy block
#   * routine-declarator:sym<sub>: `lambda` declares a sub
#   * term:sym<identifier> / methodop: `foo (1, 2)` is a two-argument call
#   * `₅₅` below the `use`: the lex must reach the pragma before it reads that
#   * a slang overriding what rakupp cannot apply is refused, by name
#
# Runs clean under Rakudo too (2026.08, legacy grammar).
use lib $?FILE.IO.parent.add('../fixtures/slang-lib');
use MONKEY-SEE-NO-EVAL;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my $rakupp = $*RAKU.compiler.name eq 'Raku++';

use Slang::Seams;

# ---- literal seams: the slang's token, then its action ----------------------
ck(0y101, 5, 'number:sym<bin> — the token matched and the action made the literal');
ck(0y101 + 0y11, 8, 'two of them in one expression');
ck(50%, 50, 'value:sym<pct> — `)>` trims the capture, the `%` is still consumed');
ck(₅₅, 55, 'number:sym<subs> — a construct the first lex could not read, below the use');
ck(0b101, 5, 'the built-in radix literal is untouched');

# ---- identifier ---------------------------------------------------------
sub pass?($x) { $x * 2 }
ck(pass?(21), 42, 'identifier — a routine named with a trailing ?');
ck((pass? 4), 8, '…called as a listop');
sub opt($x?) { $x? * 2 }
ck(opt(21), 42, '…and `$x?` in a signature is a variable named x? (Rakudo agrees)');

# ---- pointy-block-starter, routine-declarator -----------------------------
ck((1, 2, 3).map(λ $x { $x + 1 }).list, (2, 3, 4), 'pointy-block-starter — λ opens a pointy block');
my $l = lambda ($n) { $n * 10 };
ck($l(4), 40, 'routine-declarator:sym<sub> — `lambda` declares a sub');
sub plain($a, $b) { $a - $b }
ck(plain(5, 2), 3, 'an ordinary sub still declares');

# ---- the Tuxic modes ---------------------------------------------------------
ck(plain (7, 2), 5, 'term:sym<identifier> — `name (args)` is a call with those args');
ck(42.fmt ('-%d-'), '-42-', 'methodop — `.name (args)` is a method call');
my $if = do if (1) { 'kw' } else { 'no' };
ck($if, 'kw', 'if (…) stays an if');

# ---- sigilless-variable (Slang::Emoji): a RakuAST-grammar production ---------
if $rakupp {
    EVAL q:to/RAKU/;
    use Slang::Seams;
    my 👍 = 42;
    ck(👍 + 1, 43, 'sigilless-variable — `my 👍 = 42` declares, and 👍 is a term');
    my %h = 👍 => 'v';
    ck(%h{42}, 'v', '`👍 => v` keys on the VALUE, not an auto-quoted name');
    RAKU
}

# ---- refused by name ----------------------------------------------------
if $rakupp {
    my $msg = '';
    { EVAL 'use Slang::Refused; 1'; CATCH { default { $msg = .message } } }
    ck($msg.contains('statement-control:sym<unless>') && $msg.contains('cannot apply'), True,
       'a slang overriding a production without a seam is refused by name');
}

say $fails ?? "FAIL: $fails" !! 'PASS';
exit $fails ?? 1 !! 0;
