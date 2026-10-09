# Regression: X::Multi::NoMatch says what was called, and with what.
#
# A multi that no candidate took raised an X::Multi::NoMatch with a message
# only: `.dispatcher` and `.capture` were not there (2026-10-07). They are the
# routine and the arguments as a Capture, the invocant first for a method.
# The callers that fall back to a built-in when a user operator's candidates
# do not take core operands must still do so with the richer exception.
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $arg = "s";
multi m(Int $x) { }
try m($arg, :k(2));
my $e = $!;
check $e.^name,            'X::Multi::NoMatch', 'the type';
check $e.dispatcher.name,  'm',                 '.dispatcher is the routine';
check $e.capture.raku,     '\("s", :k(2))',     '.capture holds the arguments, named too';

class A { multi method mm(Int) { } }
try A.mm($arg);
check $!.dispatcher.name,  'mm',                'a method\'s dispatcher';
check $!.capture.raku,     '\(A, "s")',         '…and its capture starts with the invocant';

class P { has $.v }
multi infix:<+>(P $a, P $b) { P.new(v => $a.v + $b.v) }
multi prefix:<->(P $a) { P.new(v => -$a.v) }
check (P.new(v => 1) + P.new(v => 2)).v, 3, 'a user infix takes its operands';
check 1 + 2,               3,                   '…and core operands fall back to the built-in';
check -5,                  -5,                  'a user prefix falls back too';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
