# Regression: `GLOBAL::<…>:exists`, `OUR::<…>:exists` and `Pkg::<…>:exists`
# answer from the package's symbol table (review ledger O31).
#
# `GLOBAL::<$x>:exists` evaluated the name and called whatever came back an
# answer, so an undeclared `@x` or `%x` (an empty container), an undeclared
# `&x`, a built-in `&say` and a top-level `my @lex` all "existed" in GLOBAL.
# `OUR::<$x>:exists` and `Foo::<$x>:exists` dropped the adverb altogether: it
# went to the enclosing call, and `say OUR::<$z>:exists` printed
# `(Any)exists => True`. The table holds what the package has — `our`
# variables and routines, lax variables, qualified writes — and since
# 6ba94541 an `our` is installed before the code around it runs, as in Rakudo.
# Answers are Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check (GLOBAL::<@zzz>:exists, GLOBAL::<$zzz>:exists, GLOBAL::<%zzz>:exists, GLOBAL::<&zzz>:exists),
      (False, False, False, False), 'nothing declared, any sigil';
our sub g-foo { }
my @g-lex;
our @g-pk;
check (GLOBAL::<&g-foo>:exists, GLOBAL::<@g-lex>:exists, GLOBAL::<@g-pk>:exists), (True, False, True),
      'an our sub and an our array are there, a top-level my is not';
check GLOBAL::<&say>:exists, False, 'a built-in is not GLOBAL’s';
my $k = '@zzz';
check GLOBAL::{$k}:exists, False, 'a computed key';
$k = '@g-pk';
check GLOBAL::{$k}:exists, True, '…that is there';
check GLOBAL::<@zzz>:!exists, True, ':!exists';
$GLOBAL::g-auto = 3;
check GLOBAL::<$g-auto>:exists, True, 'a qualified write';
for ^1 { our %g-inner }
check GLOBAL::<%g-inner>:exists, True, 'an our inside a block';

package SP { our $x = 1; our sub ps { } }
check (SP::<$x>:exists, SP::<$y>:exists, SP::<&ps>:exists, SP::<$y>:!exists), (True, False, True, True),
      "a package's table";
class SC { our $k = 2 }
check (SC::<$k>:exists, SC::<$nope>:exists), (True, False), "a class's";
package SQ {
    check OUR::<$z>:exists, True, "OUR:: before the our has run";
    our $z;
    check (OUR::<$z>:exists, OUR::<$nz>:exists), (True, False), 'OUR:: after it';
}
our $s-top = 1;
check (OUR::<$s-top>:exists, OUR::<$s-ntop>:exists), (True, False), 'OUR:: at the top level';
package SR { our sub t { OUR::<$rv>:exists }; our $rv = 1 }
check SR::t(), True, "OUR:: in a package's sub, called from outside";

# reads keep their own path
our $s-rd = 5;
check GLOBAL::<$s-rd>, 5, 'GLOBAL::<$x> reads the variable';
SP::<$w> = 9;
check $SP::w, 9, 'Foo::<$x> = … still makes the slot';

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL' } else { say 'PASS' }
