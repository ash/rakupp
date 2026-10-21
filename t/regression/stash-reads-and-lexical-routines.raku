# Regression: a lexical routine is not GLOBAL's symbol, and `GLOBAL::{'…'}`
# reads and writes GLOBAL's table as a Stash (review ledger O32).
#
# A plain `sub f` (or `my sub f`, or a plain `multi`) is lexical, and an
# `our sub` the package's, but a mainline routine sits in the same table
# either way and nothing recorded which it was: `GLOBAL::<&f>:exists` was
# True for both. A routine now remembers that it was declared without `our`.
# `GLOBAL::{'@x'}` with a string key read the way `::('@x')` does — a
# Failure (X::NoSuchSymbol) for a missing symbol, and a lexical `my @x` when
# there was one — where a Stash's missing key is Any; and assigning to
# `GLOBAL::{'$x'}` died X::Undeclared instead of making the slot.
# Answers are Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

sub lr-bar { }
our sub lr-foo { 42 }
my sub lr-mb { }
multi lr-mm(Int) { }
our proto lr-op(|) {*}
multi lr-op(Int) { }
check (GLOBAL::<&lr-bar>:exists, GLOBAL::<&lr-foo>:exists), (False, True), 'a plain sub is lexical, an our sub is not';
check (GLOBAL::<&lr-mb>:exists, GLOBAL::<&lr-mm>:exists, GLOBAL::<&lr-op>:exists), (False, False, True),
      'my sub and a plain multi are lexical, an our proto is not';
our &lr-oa = sub { 1 };
check GLOBAL::<&lr-oa>:exists, True, 'an our code variable';
our &lr-alias = &lr-bar;
check GLOBAL::<&lr-alias>:exists, True, '…holding a lexical routine';
package LRP { our sub ps { 1 } }
check (GLOBAL::<&ps>:exists, LRP::<&ps>:exists), (False, True), "a package's our sub is the package's";

check GLOBAL::{'@lr-zzz'}.^name, 'Any', 'a missing key reads Any';
our @lr-pk;
check GLOBAL::{'@lr-pk'}.^name, 'Array', 'an our array reads itself';
my @lr-lex;
check GLOBAL::{'@lr-lex'}.^name, 'Any', 'a top-level my is not there';
check GLOBAL::{'&lr-bar'}.^name, 'Any', 'nor a lexical routine';
check GLOBAL::{'&lr-foo'}(), 42, 'an our routine is, and calls';
my $k = '$lr-v';
our $lr-v = 3;
check GLOBAL::{$k}, 3, 'a computed key';
$GLOBAL::lr-auto = 3;
check GLOBAL::{'$lr-auto'}, 3, 'a qualified write';
class LRFoo { }
check GLOBAL::{'LRFoo'}.^name, 'LRFoo', 'a sigilless key is a type, as before';

GLOBAL::{'$lr-nw'} = 7;
check $GLOBAL::lr-nw, 7, 'assigning makes the slot';
check GLOBAL::<$lr-nw>:exists, True, '…which then exists';
our $lr-e1 = 1;
GLOBAL::{'$lr-e1'} = 5;
check $lr-e1, 5, '…or writes the our that is there';

my $p = run $*EXECUTABLE, '-e', Q[use Test; say GLOBAL::<&ok>:exists; say GLOBAL::{'&ok'}.^name], :out;
check $p.out.slurp(:close).lines.List, ('False', 'Any'), 'an import is lexical too';

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL' } else { say 'PASS' }
