# Regression: an `is rw` / `is raw` / `\x` parameter writes through to an
# argument that reached it FLATTENED, `f(|@a)`, `f(|$capture)`, `f(|%h)`.
# Binding linked a parameter to the argument EXPRESSION in the same position,
# and `|@a` is one expression for every element of @a, so nothing was written
# back (and past a slip the positions named the wrong expressions: a write could
# land in the next variable along). The flattened argument's own container is
# now found (FlatRw in src/InterpreterCalls.cpp): an Array element, a Hash
# entry, a container a List or Capture held, and for a routine's `|c` forwarded
# on, the arguments of the call that made it.
#   1. positional, named, raw, sigilless, slurpy, methods, multis
#   2. a `|c` capture forwarded through wrappers
#   3. the caller's array is an ordinary array again once the call returns
#   4. what is not flattened, or not a container, is as it was
# Every expected value here is what Rakudo 2026.09 prints for the same program.
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eqv $want }

# 1. ---------------------------------------------------------------------
sub f($x is rw, *@) { $x = 5 }
my @a = 1; f(|@a); check @a, [5], 'array element';
my $y = 1; my $c = \($y); f(|$c); check $y, 5, 'capture literal part';
my @b = 1, 2; sub m2($p is rw, $q is rw) { $p = 10; $q = 20 }; m2(|@b); check @b, [10, 20], 'two elements';
my $s = 1; my @c = $s; f(|@c); check "$s @c[]", '1 5', 'an Array holds containers of its own';
sub r($x is raw) { $x = 9 }; my @e = 1; r(|@e); check @e, [9], 'is raw';
sub sl(\x) { x = 8 }; my @g = 1; sl(|@g); check @g, [8], 'sigilless';
my %h = a => 1; sub n(:$a! is rw) { $a = 7 }; n(|%h); check %h<a>, 7, 'named from a hash';
my $z = 1; my $c5 = \(:a($z)); n(|$c5); check $z, 7, 'named from a capture';
my @n = 1, 2; sub q(*@v is raw) { @v[0] = 3 }; q(|@n); check @n, [3, 2], '*@ is raw';
my @pm = 1, 2; sub u(+@v is raw) { @v[0] = 4 }; u(|@pm); check @pm, [4, 2], '+@ is raw';
my @a1 = 1; my $c3 = \(|@a1); f(|$c3); check @a1, [5], 'capture literal of a slip';
my %h4 = a => 1; my $c4 = \(|%h4); n(|$c4); check %h4<a>, 7, 'capture literal of a hash slip';
my @p = 1, 2; sub pp($a, $b is rw) { $b = 12 }; pp(|@p); check @p, [1, 12], 'second parameter';
class C { method m($x is rw, *@) { $x = 'm' }; method nm(:$k! is rw, *%) { $k = 'n' } }
my @mb = 1, 2; C.m(|@mb); check @mb, ['m', 2], 'method';
my %mh = k => 1, j => 2; C.new.nm(|%mh); check (%mh<k>, %mh<j>), ('n', 2), 'method, named';
multi mm($x is rw) { $x = 'rw' }
multi mm(Int $x) { 'int' }
my @mc = 'a'; mm(|@mc); check @mc, ['rw'], 'multi';
sub inner($y is rw, *@) { $y ~= '!' }
sub outer($x is rw, *@) { inner($x); $x ~= '?' }
my @d = 'x', 'y'; outer(|@d); check @d, ['x!?', 'y'], 'passed on';
sub rec($x is rw, $n) { $x++; rec($x, $n - 1) if $n > 0 }
my @re = 0, 3; rec(|@re); check @re, [4, 3], 'recursive';
sub twice($x is rw, *@rest) { $x = 1; again(|@rest) }
sub again($x is rw, *@) { $x = 2 }
my @tw = 0, 0, 0; twice(|@tw); check @tw, [1, 0, 0], 'a plain slurpy holds copies';
sub mixed($a, $b is rw, $c is rw) { $b = 'B'; $c = 'C' }
my @mx = 1; my $mz = 0; mixed(|@mx, |@mx, $mz); check "@mx[] $mz", 'B C', 'two slips, then a variable';
sub after-slip($a is rw, $b is rw) { $a = 'A'; $b = 'B' }
my @as = 1; my $aw = 0; after-slip(|@as, $aw); check "@as[] $aw", 'A B', 'a variable after a slip';
my @j = 1, 2; sub lit($a is rw, $b) { $a = 9 }; lit(|(@j[0], 5)); check @j, [9, 2], 'a List literal slipped';
sub rng($a, $b is rw) { $b = 7 }; my $rv = 0; rng(|(1..1), $rv); check $rv, 7, 'after a slipped Range';
my %o = a => 2; my $ox = 1; sub over(:$a! is rw) { $a = 'o' };
over(a => $ox, |%o); check "$ox %o<a>", '1 o', 'the later named argument binds';
over(|%o, a => $ox); check "$ox %o<a>", 'o o', '…either way round';
my @th = 1; await start { sub th($q is rw) { $q = 'thread' }; th(|@th) }; check @th, ['thread'], 'in a thread';
my @pb = 1; my $pr := @pb[0]; sub vp($x is rw) { $x = 'P' }; vp(|@pb); check "@pb[] $pr", 'P P', 'an element bound already';
my $keep; sub esc($x is rw, *@) { $keep = -> { $x = 'late' } }
my @es = 1, 2; esc(|@es); $keep(); check @es, ['late', 2], 'a closure keeps the container';
sub inc($a is rw) { $a++ }; my @ic = 0; inc(|@ic) for ^1000; check @ic, [1000], 'many calls';

# 2. ---------------------------------------------------------------------
sub w1(|c) { f(|c) }
sub w2(|c) { w1(|c) }
my $wa = 1; w2($wa); check $wa, 5, 'capture forwarded twice';
sub lead($first, |rest) { f(|rest) }
my $lb = 1; my $lb0 = 0; lead($lb0, $lb); check "$lb0 $lb", '0 5', 'capture after a positional';
my @wo = 1, 2; w1(|@wo); check @wo, [5, 2], 'capture of a slip';
sub wn(|c) { n(|c) }
my $kv = 1; wn(a => $kv); check $kv, 7, 'named through a capture';
my %wh = a => 1; wn(|%wh); check %wh<a>, 7, 'hash through a capture';
class K { method m($x is rw, *@) { $x = 'm' }; method fw(|c) { self.m(|c) } }
my $mv = 1; K.fw($mv); check $mv, 'm', 'a method forwarding its capture';
sub two($p is rw, $q is rw) { $p = 'P'; $q = 'Q' }
sub w4(|c) { two(|c) }
my $p1 = 1; my @q2 = 2; w4($p1, |@q2); check "$p1 @q2[]", 'P Q', 'a variable and a slip through a capture';
sub deep(|c) { sub dinner(|d) { f(|d) }; dinner(|c) }
my $dv = 1; deep($dv); check $dv, 5, 'a nested sub forwarding';
sub w3(|c) { my @copy = c.list; f(|@copy) }
my $cv = 1; w3($cv); check $cv, 1, 'a copy out of the capture is a copy';

# 3. ---------------------------------------------------------------------
my @af = 1, 2, 3; f(|@af);
check @af.sort.List, (2, 3, 5), 'sorts';
check (@af.min, @af.max), (2, 5), 'min and max';
check @af.raku, '[5, 2, 3]', 'raku';
my $cl = @af.clone; $cl[0] = 77; check @af[0], 5, 'a clone is its own';
my @ar = @af.Array; @ar[0] = 66; check @af[0], 5, '.Array is its own';
my @cp = @af; @cp[0] = 99; check @af[0], 5, 'a copy is its own';
check (@af »+» 1).List, (6, 3, 4), 'hyper';
my @dd = 3, 1, 2; sub dies($x is rw, *@) { $x = 5; die "boom" }; try dies(|@dd);
check @dd.sort.List, (1, 2, 5), 'sorts after the callee died';
my %ah = a => 1, b => 2; sub nb(:$a! is rw, *%) { $a = 7 }; nb(|%ah);
my %ac = %ah; %ac<a> = 9; check %ah<a>, 7, 'a hash copy is its own';
check %ah eqv {a => 7, b => 2}, True, 'the hash compares as before';

# 4. ---------------------------------------------------------------------
sub g(*@args) { f(|@args) }
my $gz = 1; g($gz); check $gz, 1, 'a slurpy makes fresh containers';
my $dz = 1; my $dc = \($dz, 2, :k(3)); $dc[0]++; check $dz, 2, 'a capture literal keeps a variable';
check $dc.raku, '\(2, 2, :k(3))', 'and reads as before';
my @x9 = 1, 2; my $c9 = \(|@x9, 9); check $c9.list, (1, 2, 9), 'a capture of a slip lists its parts';
sub plain($a, $b) { $a + $b }; my @pl = 1, 2; check plain(|@pl), 3, 'a plain call through a slip';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
