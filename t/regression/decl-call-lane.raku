# Regression guard: `my $x = <method call>` on the declaration lane.
#
# `my $p = Point.new(…)` and `my $n = $obj.count` went through evalAssignInner's
# general path, about 4,100 instructions for the declaration alone, a tenth of
# tools/bench/objects-steady.raku. Interpreter::declCallLane makes the call and
# stores a plain answer (an untagged Int, Num, Str or Bool, or an instance) as
# the declaration lane stores a literal; any other answer is held for the
# general path, which takes it where it would have called. So what must stay
# true is that the declared variable holds exactly what it held before, for
# every kind of answer: itemized lists and hashes, a kept Seq, a Failure, Nil,
# a type object, an enum value, a native, a junction, a container handed back
# by an `is rw` method (copied, not aliased), and a fresh variable each time
# round a loop. RAKUPP_DECLCALL_CHECK=1 compares the two paths on every such
# declaration a program makes.

my @got;
sub o(**@a) { @got.push: @a.map(*.gist).join }
enum Colour <red green blue>;
class P {
    has $.x; has $.r is rw = 1; has @.items = 1, 2, 3;
    method list   { (1, 2, 3) }
    method arr    { [4, 5] }
    method hash   { {a => 1} }
    method seq    { (1..3).map(* * 2) }
    method nil    { Nil }
    method failed { fail 'no' }
    method str    { 'str' }
    method me     { self }
    method type   { Int }
    method enumv  { green }
    method pair   { k => 'v' }
    method junc   { 1 | 2 }
    method cont is rw { $!r }
    method native { my int $n = 7; $n }
    method big    { 2 ** 70 }
    method rat    { 3 / 4 }
    method bool   { True }
    method num    { 1.5e0 }
    method count  { @!items.elems }
}
my $o = P.new(x => 5);
for ^3 -> $round {
    my $a = $o.x;      my $b = $o.list;  my $c = $o.nil;   my $d = $o.str;   my $e = $o.me;
    my $g = $o.hash;   my $t = $o.type;  my $h = $o.arr;   my $s = $o.seq;   my $v = $o.enumv;
    my $p = $o.pair;   my $j = $o.junc;  my $n = $o.native; my $B = $o.big;  my $R = $o.rat;
    my $T = $o.bool;   my $N = $o.num;   my $k = $o.count;
    o $a, ' ', $b.raku, ' ', $c.raku, ' ', $d, ' ', $e.x, ' ', $g.raku, ' ', $t.raku, ' ', $h.raku, ' ', $s.raku;
    o $v.raku, ' ', $p.raku, ' ', $j.raku, ' ', $n, ' ', $B, ' ', $R.raku, ' ', $T, ' ', $N, ' ', $k;
    o $a.WHAT.raku, $d.WHAT.raku, $e.WHAT.raku, $n.WHAT.raku, $T.WHAT.raku, $N.WHAT.raku, $v.WHAT.raku;
    my @flat = $b;  my @harr = $h;  o @flat.elems, @harr.elems;   # a `$` holds ONE thing
    $a = $round; $d ~= '!'; o $a, $d;                               # …and is writable
    my $rw = $o.cont;  $rw += 10;  o $rw, ' ', $o.r;               # a copy of the container's value
    my $f = $o.failed;  o $f.defined, ' ', $f.WHAT.raku;
}
# a fresh variable each time round, for a closure that keeps it
my @subs;
for 1..3 -> $i { my $made = P.new(x => $i); @subs.push: { $made.x } }
o @subs.map({ .() }).join(',');
# the declaration is an expression: its value, and its container
o (my $val = $o.str).uc;
(my $lv = $o.x) = 9;  o $lv, ' ', $o.x;
sub take($y) { $y } ;  o take(my $arg = $o.count);
# a control word on the right keeps the general path's guarded store
my @seen;
for 1..4 -> $i { my $w = ($i %% 2 ?? Nil !! $o.str) // next;  @seen.push: $w ~ $i }
o @seen.join(' ');
# built-in invocants answer through the same lane
my $u = 'abc'.uc;  my $i = '42'.Int;  my $r = 3.5.round;  my $w = 'a b'.words;  my $l = (3, 1, 2).sort;
o $u, ' ', $i, ' ', $r, ' ', $w.raku, ' ', $l.raku, ' ', $i.WHAT.raku;
# a call that dies stores nothing
my $z = 'kept';
try { my $z = P.new(x => 1).nosuch; o 'not reached' }
o $z, ' ', $!.^name;

my @want = q:to/WANT-END/.lines;
5 $(1, 2, 3) Any str 5 ${:a(1)} Int $[4, 5] $((2, 4, 6).Seq)
Colour::green :k("v") any(1, 2) 7 1180591620717411303424 0.75 True 1.5 3
IntStrPIntBoolNumColour
11
0str!
11 1
False Failure
5 $(1, 2, 3) Any str 5 ${:a(1)} Int $[4, 5] $((2, 4, 6).Seq)
Colour::green :k("v") any(1, 2) 7 1180591620717411303424 0.75 True 1.5 3
IntStrPIntBoolNumColour
11
1str!
11 1
False Failure
5 $(1, 2, 3) Any str 5 ${:a(1)} Int $[4, 5] $((2, 4, 6).Seq)
Colour::green :k("v") any(1, 2) 7 1180591620717411303424 0.75 True 1.5 3
IntStrPIntBoolNumColour
11
2str!
11 1
False Failure
1,2,3
STR
9 5
3
str1 str3
ABC 42 4 $(("a", "b").Seq) $((1, 2, 3).Seq) Int
kept X::Method::NotFound
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
