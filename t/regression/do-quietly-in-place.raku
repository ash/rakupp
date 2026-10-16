# Regression: `do { … }` and `quietly { … }` run their block where it stands,
# as `try { … }` does (try-in-place.raku), instead of building a closure and
# calling it on every evaluation. What the call gave the block has to survive:
# the value, `return`, `next`/`last`, the topic, lexicals and the closures that
# capture them, a `die` or a Failure passing through untouched (only `try` is
# under `use fatal`), the frame `warn` prints — and, for a body that needs the
# call (`state`, `temp`, a phaser, a `when`), the call itself.
#
# Every check verified against Rakudo (this file runs on both engines).
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# the value, lexicals, the topic
check (do { my $a = 1; $a + 41 }), 42, 'the last statement is the value';
my $s = 7;
check (do { my $s = 2; $s * 10 }), 20, 'a my inside do shadows';
check $s, 7, '…and goes with the block';
do { $s = 9 };
check $s, 9, 'an outer variable is written through';
my @a = 1, 2, 3;
for @a { do { $_ *= 2 } }
check @a, [2, 4, 6], 'the loop topic is aliased inside do';
my @c;
for 1..3 { do { my $k = $_ * 10; @c.push: { $k } } }
check @c».(), [10, 20, 30], 'closures made in a do keep their own scope';
check (do { my @l = 1, 2; @l }).elems, 2, 'a do ending on an array';
check (do { do { try { die "x" }; "ok " ~ $!.message } }), 'ok x', 'do and try nested';
class C { has $.n = 3; method m { do { self.n * 2 } } }
check C.new.m, 6, 'self inside do in a method';
my $*dd = 1;
sub dg { $*dd }
check (do { my $*dd = 2; dg() }), 2, 'a dynamic declared in do is seen by a call from it';
check dg(), 1, '…and not after it';

# return, next and last reach their routine and loop
sub r1 { my $x = do { return 5 }; 6 }
check r1(), 5, 'return inside do returns from the routine';
my @seen;
for 1..5 { do { next if $_ == 2; last if $_ == 4 }; @seen.push($_) }
check @seen, [1, 3], 'next and last inside do drive the loop';

# only `try` catches, and only `try` makes a Failure fatal
my $caught = try { do { die "inner" } };
check $!.message, 'inner', 'a die inside do reaches the try around it';
sub mkfail { fail "mf" }
my $f = do { mkfail() };
check $f.^name, 'Failure', 'a Failure passes through a do';
check ($f // 'dflt'), 'dflt', '…undefined, and not thrown';
$f.so;

# quietly
check (quietly { warn "hidden"; 42 }), 42, 'quietly answers its block';
check (quietly { do { my $z = 3; $z } }), 3, 'quietly around a do';

# bodies that keep the call
sub st { do { state $c = 0; ++$c } }
check (st(), st()), (1, 1), 'state inside do is per evaluation';
my $left;
do { my $l = 4; LEAVE { $left = $l } };
check $left, 4, 'a LEAVE inside do runs';
given 5 { check (do { when 5 { "five" } }), 'five', 'a when inside do ends the do' }
my $gg = 1;
sub readg { $gg }
sub tf { do { temp $gg = 2; readg() } }
check tf(), 2, 'temp inside do';
check $gg, 1, '…is restored when the block leaves';

# threads
my @sums = await (^4).map: { start { my $t = 0; for ^500 { $t += do { my $u = $_; $u } }; $t } };
check @sums, [124750 xx 4], 'do blocks on several threads at once';

# the backtrace still has the do block as a frame, and quietly still silences
my $w = run($*EXECUTABLE, '-e', 'my $x = do { warn "careful"; 1 }', :out, :err);
check $w.err.slurp(:close).lines[1].trim, 'in block  at -e line 1', 'warn inside do names the block';
my $q = run($*EXECUTABLE, '-e', 'quietly { warn "shh" }; print "out"', :out, :err);
check ($q.out.slurp(:close), $q.err.slurp(:close)), ('out', ''), 'quietly keeps the warning off stderr';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
