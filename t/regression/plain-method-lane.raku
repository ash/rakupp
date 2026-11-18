# Regression guard: a plain method's lean call lane.
#
# invokeMethod is written for everything a method may be — a native call, a
# wrapped routine, a multi's dispatcher, a body with phasers, a CATCH, nested
# subs, an `is rw` return, a signature of named, optional, slurpy and
# attributive parameters. A method that is none of that (`method len2() { … }`,
# `method shift($d) { … }`, a multi candidate `(Int $x where * > 0)` its
# dispatcher has judged) takes Interpreter::callPlainMethod from its second
# call on: the frame, `self`, the arguments in their slots, the statements.
# What must stay true is every way such a method is left and every call the
# lane has to decline: `return` from a loop, a `when`, a `die` the caller
# catches, `temp` and `let` undone, `state` kept, recursion, `nextsame` and
# `callsame`, the method's own `$_`, an argument of the wrong type, a named,
# surplus, missing, Junction or Mu argument, a Buf written through, a
# parenthesized list keeping its variables' containers, a role's parameter
# read in the body, and a sub called from the body. Each shape is called
# three times: the first call settles what the lane is decided from.

my @got;
sub o(**@a) { @got.push: @a.map(*.gist).join }
our $dyn = 'outer';
sub helper($v) { $v * 2 }
role Scaled[$factor] { method scaled($n) { $n * $factor } }
class Base { method chained($n) { "Base($n)" } }
class P is Base does Scaled[3] {
    has $.x = 1; has $.count is rw = 0; has @.log;
    method none() { 42 }
    method bare { 'bare' }
    method one($a) { $a }
    method two($a, $b) { "$a/$b" }
    method typed(Int $i, Str $s) { "$i:$s" }
    method num(Num $n) { $n * 2 }
    method guarded(Int $x where * > 0) { $x }
    method ret-loop($limit) { for 1..10 -> $i { return $i if $i >= $limit }; 'none' }
    method ret-nested($n) { for 1..3 -> $i { for 1..3 -> $j { return "$i$j" if $i * $j == $n } }; 'none' }
    method when-it($v) { given $v { when 1 { 'one' }; when Str { 'str' }; default { 'other' } } }
    method topic-when($v) { $_ = $v; when 2 { 'two' }; 'not two' }
    method dies($v) { die "died $v" if $v; 'lived' }
    method tmp { temp $dyn = 'inner'; self.read-dyn }
    method read-dyn { $dyn }
    method counted { state $n = 0; ++$n }
    method fact($n) { $n <= 1 ?? 1 !! $n * self.fact($n - 1) }
    method chained($n) { 'P>' ~ callsame() }
    method chained2($n) { nextsame }
    method uses-topic { $_ = 5; $_ + 1 }
    method sets($v) { $!x = $v; self }
    method bump { $!count++; $!count }
    method calls-sub($v) { helper($v) + helper($v) }
    method list-arg($l) { $l.elems }
    method keep($l) { $l[0] = 'written'; $l }
    method last-is-return($v) { my $t = $v + 1; return $t * 2 }
    method empty-body() { }
    method implicit-nil($v) { if $v { 'yes' } }
    method me() { self }
    method routine-name() { &?ROUTINE.name }
    method leaves($v) { $v.elems }
    method buf($b) { $b[0] = 99; $b.elems }
    method closure($n) { -> $m { $n + $m + $!x } }
    multi method mm(Int $x where * > 0) { "pos $x" }
    multi method mm(Int $x) { "int $x" }
    multi method mm(Str $s) { "str $s" }
}
my $p = P.new;
for ^3 -> $round {
    o $p.none, $p.bare, $p.one(1), $p.one('s'), $p.one((1, 2)).raku, $p.one([1, 2]).raku, $p.one(Nil).raku, $p.one(Int).raku;
    o $p.two(1, 2), ' ', $p.typed(1, 'a'), ' ', $p.num(1.5e0), ' ', $p.guarded(3), ' ', $p.scaled(4);
    o $p.ret-loop(3), $p.ret-loop(99), ' ', $p.ret-nested(4), $p.ret-nested(7);
    o $p.when-it(1), $p.when-it('x'), $p.when-it(2.5), ' ', $p.topic-when(2), ' ', $p.topic-when(3);
    o $p.dies(0), ' ', (try $p.dies(1)) // "caught: $!";
    o $p.tmp, ' ', $dyn, ' ', $p.counted, ' ', $p.fact(10), ' ', $p.chained(1), ' ', $p.chained2(2).raku;
    $_ = 'mine';  o $p.uses-topic, ' ', $_;
    o $p.sets($round).x, ' ', $p.bump, ' ', $p.calls-sub(3), ' ', $p.last-is-return(1), ' ', $p.empty-body.raku, ' ',
      $p.implicit-nil(0).raku, ' ', $p.implicit-nil(1), ' ', $p.me === $p, ' ', $p.routine-name;
    o $p.closure(1)(2), ' ', $p.mm(1), ' ', $p.mm(0), ' ', $p.mm(-1), ' ', $p.mm('s');
    # the calls the lane declines
    o (try $p.typed('x', 'a')) // "type: {$!.^name}";
    o (try $p.guarded(-1)) // "where: {$!.^name}";
    o (try $p.one()) // "too few: {$!.^name}";
    o (try $p.one(1, 2)) // "too many: {$!.^name}";
    o (try $p.none(1)) // "too many: {$!.^name}";
    o $p.one(1, :named).raku, ' ', $p.one(1|2).raku, ' ', (try $p.one(Mu)) // "Mu: {$!.^name}";
    o $p.typed(2 ** 70, 's'), ' ', (try $p.num(1)) // "Int for Num: {$!.^name}";
    my $a = 1;  my $kept = $p.keep(($a, 2));  o $a, ' ', $kept.raku;
    my $buf = Buf.new(1, 2, 3);  o $p.buf($buf), ' ', $buf[0];
    my @arr = 1, 2, 3;  o $p.leaves(@arr), $p.list-arg(@arr), $p.list-arg((1, 2)), $p.list-arg(5);
    my int $native = 7;  my $back = $p.one($native);  o $back, ' ', $back.WHAT.raku;
    o P.none, ' ', P.one(5), ' ', (try P.sets(1)) // 'no attributes in a type object';
}
# a method left by an exception leaves the caller's scope as it was
my $outer = 'kept';
sub through { my $inner = 'in sub'; try { $p.dies(1) }; $inner }
o through(), ' ', $outer;
# deep recursion through the lane, and a call from another thread
o $p.fact(200).chars;
o await(start { $p.two('a', 'b') }), ' ', (^4).race(:batch(1)).map({ $p.one($_) }).sort.join;

my @want = q:to/WANT-END/.lines;
42bare1s$(1, 2)$[1, 2]NilInt
1/2 1:a 3 3 12
3none 22none
onestrother two not two
lived caught: died 1
inner outer 1 3628800 P>Base(1) Nil
6 mine
0 1 12 4 Nil Empty yes True routine-name
3 pos 1 int 0 int -1 str s
type: X::TypeCheck::Binding::Parameter
where: X::TypeCheck::Binding::Parameter
too few: X::AdHoc
too many: X::AdHoc
too many: X::AdHoc
1 any(1, 2) Mu: X::TypeCheck::Binding::Parameter
1180591620717411303424:s Int for Num: X::TypeCheck::Binding::Parameter
written $("written", 2)
3 99
3321
7 Int
42 5 no attributes in a type object
42bare1s$(1, 2)$[1, 2]NilInt
1/2 1:a 3 3 12
3none 22none
onestrother two not two
lived caught: died 1
inner outer 2 3628800 P>Base(1) Nil
6 mine
1 2 12 4 Nil Empty yes True routine-name
4 pos 1 int 0 int -1 str s
type: X::TypeCheck::Binding::Parameter
where: X::TypeCheck::Binding::Parameter
too few: X::AdHoc
too many: X::AdHoc
too many: X::AdHoc
1 any(1, 2) Mu: X::TypeCheck::Binding::Parameter
1180591620717411303424:s Int for Num: X::TypeCheck::Binding::Parameter
written $("written", 2)
3 99
3321
7 Int
42 5 no attributes in a type object
42bare1s$(1, 2)$[1, 2]NilInt
1/2 1:a 3 3 12
3none 22none
onestrother two not two
lived caught: died 1
inner outer 3 3628800 P>Base(1) Nil
6 mine
2 3 12 4 Nil Empty yes True routine-name
5 pos 1 int 0 int -1 str s
type: X::TypeCheck::Binding::Parameter
where: X::TypeCheck::Binding::Parameter
too few: X::AdHoc
too many: X::AdHoc
too many: X::AdHoc
1 any(1, 2) Mu: X::TypeCheck::Binding::Parameter
1180591620717411303424:s Int for Num: X::TypeCheck::Binding::Parameter
written $("written", 2)
3 99
3321
7 Int
42 5 no attributes in a type object
in sub kept
375
a/b 0123
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
