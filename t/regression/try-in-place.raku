# Regression: `try { … }` runs its block where it stands, in a scope of its
# own, instead of building a closure and calling it on every evaluation
# (SPEED-BACKLOG 38: the call was most of a `try` block's cost). What the call
# gave the block has to survive the change: the value, `$!`, `return`,
# `next`/`last`, the topic, lexicals and the closures that capture them, a
# frame in the backtrace that `warn` prints — and, for a body that needs the
# call (`state`, `temp`, a phaser, CATCH, `fail`, a `when`), the call itself.
#
# Every check verified against Rakudo (this file runs on both engines).
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# the value, and $!
check (try { my $x = 1; $x += 1; $x + 40 }), 42, 'the last statement is the value';
check $!, Nil, '$! is Nil after a try that lived';
check (try { die "boom" }), Nil, 'a try that dies is Nil';
check $!.message, 'boom', '…and $! holds the exception';
try { die X::AdHoc.new(payload => 'p') };
check $!.payload, 'p', 'a typed exception reaches $!';
sub mkfail { fail "mf" }
check (try { mkfail() }), Nil, 'a Failure the block ends on is consumed';
check $!.message, 'mf', '…and is $!';
check (try { mkfail() // 'caught' }), 'caught', 'only the try block is under `use fatal`, not what it calls';
check (try { }).raku, 'Nil', 'an empty try is Nil';
check (try { no fatal; +"zz" }).^name, 'Failure', '`no fatal` keeps the Failure';

# return, next and last reach their routine and loop
sub r1 { try { return 5 }; 6 }
check r1(), 5, 'return inside try returns from the routine';
sub r2 { my $x = try { return 7 if True; 8 }; "after $x" }
check r2(), 7, '…from the middle of an assignment too';
my @seen;
for 1..5 { try { next if $_ == 2; last if $_ == 4 }; @seen.push($_) }
check @seen, [1, 3], 'next and last inside try drive the loop';

# lexicals, the topic, and closures
my $outer = 1;
try { my $outer = 2; $outer++ };
check $outer, 1, 'a my inside try shadows, and goes with the block';
try { $outer = 10 };
check $outer, 10, 'an outer variable is written through';
my @a = 1, 2, 3;
for @a { try { $_ *= 2 } }
check @a, [2, 4, 6], 'the loop topic is aliased inside try';
my @c;
for 1..3 { try { my $k = $_ * 10; @c.push: { $k } } }
check @c».(), [10, 20, 30], 'closures made in a try keep their own scope';
check (try { try { die "inner" }; "survived " ~ $!.message }), 'survived inner', 'nested try';
class C { has $.n = 3; method m { try { self.n * 2 } } }
check C.new.m, 6, 'self inside try in a method';
sub depth($n) { $n == 0 ?? 0 !! 1 + (try { depth($n - 1) }) }
check depth(50), 50, 'try in a recursion';
my $*dyn = 1;
sub g { $*dyn }
my $dynSeen = try { my $*dyn = 2; g() };
check $dynSeen, 2, 'a dynamic declared in try is seen by a call from it';
check g(), 1, '…and not after it';
my $ev = try { EVAL '1 + 1' };
check $ev, 2, 'EVAL inside try';

# bodies that keep the call
my $gg = 1;
sub readg { $gg }
sub tf { try { temp $gg = 2; readg() } }
check tf(), 2, 'temp inside try';
check $gg, 1, '…is restored when the block leaves';
sub st { try { state $s = 0; ++$s } }
check (st(), st()), (1, 1), 'state inside try is per evaluation';
check (try { die "c"; CATCH { default { } } }).raku, 'Nil', 'an explicit CATCH handles it';
check (try { try { CATCH { default { die "wrapped" } }; die "orig" }; "no" } // $!.message),
      'wrapped', 'a CATCH that rethrows propagates past the try';
sub fl { try { fail "ff" } }
check fl().raku, 'Nil', 'fail inside try is caught by it';
my $left;
try { my $l = 3; LEAVE { $left = $l } };
check $left, 3, 'a LEAVE inside try runs';

# threads: each worker decides and runs its own try blocks
my @sums = await (^4).map: { start { my $s = 0; for ^1000 { try { $s += $_ } }; $s } };
check @sums, [499500 xx 4], 'try blocks on several threads at once';

# the backtrace still has the try block as a frame: `warn` names it
my $w = run($*EXECUTABLE, '-e', 'try { warn "careful" }', :out, :err);
check $w.err.slurp(:close).lines[1].trim, 'in block  at -e line 1', 'warn inside try names the block';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
