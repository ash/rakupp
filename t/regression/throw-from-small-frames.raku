# SPEED-BACKLOG 55 (2026-11-06): an error is raised where the frames are
# small. libc++abi scans each frame's call-site table linearly, in both unwind
# passes, and a frame's cleanup resumes the unwind from its landing pad, often
# at the frame's end: one `.throw` crossed methodCallPart2 (46 KB of table)
# and methodCallInner (21 KB), 4.0M instructions. Now `.throw` is RETURNED to
# methodCall, tagged, and raised there; a user object's own method is asked
# for from methodCall, so methodCallInner is not under the methods it runs; and
# the built-in `die` is raised by execDieStmt, ahead of anything of evalCall's.
# What must not change, each answer here Rakudo's: CONTROL and `.resume`,
# `.rethrow`, `.fail`, a class's own `throw`, the backtrace, a `die` of no
# argument (the `$!` it raises again), of an object, of a list and of a lazy
# Seq, a program's own `die` and a wrapped one, a `die` inside an expression,
# LEAVE against an outer CATCH.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

class X::Mine is Exception { method message { "mine" } }
class OwnThrow { method throw { "own throw ran" } }
class K { method m { X::AdHoc.new(payload => "in method").throw }; method d { die "died in method" } }
sub inner { X::Mine.new.throw }
sub outer { inner() }

try { outer() }
check($!.^name, 'X::Mine', '.throw in a sub');
check($!.message, 'mine', '…its message, from its own method');
check($!.backtrace.list.grep(*.subname).map(*.subname).head(3).List, ('throw', 'inner', 'outer'),
      '…and the frames of its backtrace');
try { K.new.m }
check($!.message, 'in method', '.throw in a method');
my $e = X::AdHoc.new(payload => "held");
try { $e.throw }; try { $e.throw };
check($!.message, 'held', 'the same object thrown twice');
check(OwnThrow.new.throw, 'own throw ran', "a class's own throw wins");
my $r = do { CONTROL { default { .resume } }; X::Control.new.throw; "resumed" };
check($r, 'resumed', 'a control exception resumed by CONTROL');
check((try { try { X::Mine.new.throw }; $!.rethrow }) // $!.message, 'mine', '.rethrow');
sub failing { X::Mine.new.fail }
check(failing().exception.message, 'mine', '.fail returns a Failure');

try { K.new.d }
check($!.message, 'died in method', 'die in a method');
sub plain { die "plain" }
try { plain() }
check(($!.^name, $!.message), ('X::AdHoc', 'plain'), 'die in a sub');
sub of-obj { die X::Mine.new }
try { of-obj() }
check($!.^name, 'X::Mine', 'die of an exception object');
sub of-list { die "a", "b", 3 }
try { of-list() }
check($!.message, 'ab3', 'die of a list');
sub of-seq { die (1..3).map(* * 2) }
try { of-seq() }
check($!.message, '2 4 6', 'die of a lazy Seq');
check((try { die }) // $!.message, '2 4 6', 'die of nothing raises $! again');
sub in-expr($x) { $x // die "undefined" }
check(in-expr(5), 5, 'die in an expression, not reached');
check((try in-expr(Any)) // $!.message, 'undefined', '…and reached');
my @order;
sub with-leave { LEAVE @order.push('leave'); die "x" }
{ with-leave(); CATCH { default { @order.push('catch') } } }
check(@order.List, ('catch', 'leave'), 'an outer CATCH runs before the LEAVE');
my $resumed = 0;
sub resumable { die "r"; $resumed = 1; 'after' }
my $got = do { CATCH { default { .resume } }; resumable() };
check(($got, $resumed), ('after', 1), 'a die resumed by an outer CATCH');
{
    sub die(*@a) { "own die: @a[]" }
    check(die("x"), 'own die: x', "the program's own die");
}
my $wrapped = 0;
my $h = &die.wrap(-> |c { $wrapped++; callsame });
try { die "w" }
check(($wrapped, $!.message), (1, 'w'), 'a wrapped die');
$h.restore;

if @fail { note $_ for @fail; say "FAIL" } else { say "PASS" }
exit @fail ?? 1 !! 0;
