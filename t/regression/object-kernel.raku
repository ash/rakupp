# Regression guard: objects a loop makes and never lets out, held as slots.
#
# `my $p = Point.new(x => A, y => B)` in a loop a kernel runs made an object
# per iteration — two thirds of tools/bench/objects-steady.raku — and any
# mention of a class kept the whole loop out of a kernel. A loop kernel
# (IntKernel.cpp, OBJECT-KERNEL-PLAN.md) now holds such an object as frame
# slots, one an attribute, reads `$p.x` as the slot and compiles `$p.m(…)` in
# place when m's body is one expression. What must stay true: the answers are
# those of the same loop with RAKUPP_NO_KERNELS=1 and of Rakudo — for a
# class the plain fill does not cover (a typed or defaulted attribute, BUILD,
# TWEAK, a `new` of its own), a method the kernel cannot compile in place (one
# that reads a variable outside itself, calls itself, has statements, a
# `callsame`), an object that leaves the loop, an attribute never given, an
# argument no attribute takes, a number that outgrows int64 — for an object
# made before the loop and only read in it (its attributes copied into slots
# at each entry), whatever its attributes hold at the next entry, and one the
# loop writes or stores — and for a loop
# entered again after a class was declared or a method wrapped, and for a
# class that is a scope's own (`my class`). Since phase 3 such a `my` may be
# assigned another object of its class (`$p = $p.shift(1)`: the name keeps its
# own slots, so `my $q = $p` before it still reads the old object), `?? !!`
# may choose between two, and a method's body may be statements — `my`, `if`,
# `return` — whose ends all answer one kind of value; an assignment or a
# `?? !!` that would change a class or an attribute's type, and a body whose
# ends disagree or answer a Str or a Rat, leave the loop as it was.
# Since phase 5 a variable made before the loop may be given a fresh object
# of its class in it (`$p = $p.shift(1)`): the object is made once, when the
# loop ends, and only if the loop assigned — so a loop that never does leaves
# the very object there, one that would hand over an object with a name
# (`$p = $p.me`, `$p = $t`) runs as before, and so does one whose variable is
# typed, bound, or a second name for the container.
# And an object may leave the loop: pushed onto an array, or handed by name
# to a variable of the program. It is made where it is pushed, or — one with
# a name — where the name is declared or assigned, so the same name pushed
# twice, a copy of it, and the variable it is handed to all hold one object.
# An attribute may be typed Int, Num, Str or Bool (phase 7): the plain fill
# takes an argument that is exactly of that type, in the loop and out of it,
# and everything else — a wrong type and its error, a Bool for an Int, an
# IntStr, a big Int, Nil, a type object, a smiley, a subset, a coercion, a
# native — is the general construction's, as before.
# And an attribute may be written (phase 4) — `$!n = …`, `$!n++`, `$!n += …`
# in a method, `$p.n = …` through an `is rw` accessor — as a store to its
# slot, for an object only one name holds; an object of the program gets its
# attributes back when the loop ends. Two names that may hold one object, an
# object that leaves the loop, an accessor that is not rw, a value of another
# type: the loop as before.

my @got;
sub o(**@a) { @got.push: @a.map(*.gist).join }

class Point {
    has $.x;
    has $.y;
    method len2()     { $.x * $.x + $.y * $.y }
    method shift($d)  { Point.new(x => $.x + $d, y => $.y - $d) }
    method priv()     { $!x - $!y }
    method twice()    { self.len2 + $.len2 }
    method me()       { self }
    method scaled(Int $k) { Point.new(x => $!x * $k, y => $!y * $k) }
    method pick($c)   { $c ?? $.x !! $.y }
}
class Dflt   { has $.a; has $.w = 7; has $.f = 1.5e0; method sum() { $.a + $.w }; method fl() { $.f * 2 } }
class Named  { has $.name; has $.n; method tag() { $.name ~ '#' ~ $.n }; method len() { $.name.chars + $.n } }
class Over   { has $.x; method x() { 42 }; method get() { $.x + $!x } }
class Priv   { has $!z; has $.a; method z() { $!z } }
class Typed  { has Int $.x; method dbl() { $.x * 2 } }
class Built  { has $.x; submethod BUILD(:$x) { $!x = $x * 10 }; method get() { $.x } }
class Tweak  { has $.x; submethod TWEAK() { $!x = $!x + 1 }; method get() { $.x } }
class OwnNew { has $.x; method new($v) { self.bless(x => $v) }; method get() { $.x } }
class Rec    { has $.n; method down() { $.n <= 0 ?? 0 !! Rec.new(n => $.n - 1).down + 1 } }
class Stmts  { has $.x; method m() { my $t = $.x * 2; $t + 1 } }
my $outer = 100;
class Free   { has $.x; method m() { $.x + $outer } }
class Kid is Point { has $.z; method vol() { $.x * $.y * $.z } }
class Kid2 is Point { method len2() { callsame() + 1 } }
role Sized   { has $.size; method big() { $.size > 10 } }
class Box does Sized { has $.w; method area() { $.w * $.size } }
class Elems  { has $.elems; has $.keys; method both() { $.elems + $.keys } }
class Fx     { has $.v; method bump() { $!v = $!v + 1 } }
class Wrapped { has $.x; method val() { $.x + 1 } }
class Step {
    has $.x; has $.y;
    method shift($d)  { Step.new(x => $.x + $d, y => $.y + $d) }
    method swap()     { Step.new(x => $!y, y => $!x) }
    method len2()     { $.x * $.x + $.y * $.y }
    method me()       { self }
    method clamp($m)  { if $.x > $m { return Step.new(x => $m, y => $.y) }; self }
    method norm1()    { my $a = $.x < 0 ?? -$.x !! $.x; my $b = $.y < 0 ?? -$.y !! $.y; $a + $b }
    method sign()     { if $.x > 0 { 1 } elsif $.x < 0 { -1 } else { 0 } }
    method big($n)    { return True if $.x > $n; False }
    method step($n)   { my $q = self.shift($n); $q = $q.swap if $n %% 2; $q }
    method half()     { return $.x / 2e0 if $.x > 3; $.y * 1e0 }
    method deep($n)   { if $n > 5 { if $.x > $n { return 1 }; return 2 }; my $s = self.sign; return $s + 10 if $s; 3 }
    method outer2($n) { my $a = self.deep($n); my $b = self.clamp(3).deep($n); $a * 100 + $b }
    method mixed()    { return 1.5e0 if $.x > 3; $.y }
    method either()   { return self if $.x > 3; $.y }
    method name()     { my $t = $.x + 1; "n$t" }
    method rat()      { my $t = $.x + 1; $t / 3 }
    method frees()    { my $t = $.x + $outer; $t }
    method noelse()   { my $t = $.x; if $t > 3 { $t } }
    method sq()       { my $t = $.x * $.x; $t * $t }
    method unused()   { my $t = $.x; return $t; }
    method reassign($o) { my $t = $o; $t = $t.shift(1); $t.x + $o.x }
}
class Other  { has $.x; has $.y; method len2() { 1 } }
class Out {
    has $.x; has $.y; has $!z = 5; has $.w;
    method shift($d) { Out.new(x => $.x + $d, y => $.y + $d) }
    method me()      { self }
    method step()    { return Out.new(x => 0, y => $.y + 1) if $.x > 50; Out.new(x => $.x + 7, y => $.y) }
    method z()       { $!z }
    method named($s) { Out.new(x => $.x, y => $s) }
}
class Ty {
    has Int $.x; has Num $.f = 1.5e0; has Str $.s; has Bool $.b; has Int $.d = 7; has $.u;
    method len2()        { $.x * $.x + $.d }
    method shift(Int $k) { Ty.new(x => $.x + $k, f => $.f * 2e0, b => !$.b) }
    method setx($v)      { $!x = $v }
}
class TySm { has Int:D $.x = 0; has Rat $.r; has Int() $.c }
subset TyPos of Int where * > 0;
class TySu { has TyPos $.x; has int $.n }
class Wr {
    has $.n is rw; has Int $.t = 0; has $.f = 1e0; has $.s; has $.ro;
    method bump()    { $!n = $!n + 1 }
    method inc()     { $!t++ }
    method add($d)   { $!n += $d; $!t += 1; self }
    method scale($k) { $!f *= $k; $!f }
    method both($o)  { $o.n = $o.n + $!n; $!n }
    method me()      { self }
    method reset()   { $!n = 0; $!t = 0 }
    method tag($x)   { $!s = $x }
    method total()   { $!n + $!t }
}
class Vec    { has $.a; has $.b; method add($o) { Vec.new(a => $.a + $o.a, b => $.b + $o.b) }; method dot(Vec $o) { $.a * $o.a + $.b * $o.b }; method sum() { $.a + $.b }; method same($o) { $o } }

sub basic() {
    my $acc = 0;
    for 1 .. 2000 -> $i {
        my $p = Point.new(x => $i % 100, y => $i % 37);
        $acc += $p.shift(1).len2;
    }
    $acc
}
sub accessors() {
    my $s = 0; my $t = 0;
    for 1 .. 500 -> $i {
        my $p = Point.new(y => $i, x => $i * 2);
        $s += $p.x - $p.y;
        $t += $p.priv + $p.twice + $p.me.x + $p.me.me.len2;
    }
    "$s $t"
}
sub chains() {
    my $s = 0;
    for 1 .. 300 -> $i {
        my $p = Point.new(x => $i, y => 1);
        my $q = $p.scaled(3).shift($i).shift(-2);
        $s += $q.x + $q.y + $p.pick($i %% 2) + Point.new(x => $i, y => $i).len2;
    }
    $s
}
sub whiles() {
    my $s = 0; my $i = 0;
    while $i < 400 { my $p = Point.new(x => $i, y => $i + 1); $s = $s + $p.len2; $i = $i + 1 }
    loop (my $j = 0; $j < 100; $j++) { my $p = Point.new(x => $j, y => 2); $s += $p.shift($j).x }
    $s
}
sub nums() {
    my $s = 0e0; my $r = 0;
    for 1 .. 200 -> $i {
        my $p = Point.new(x => $i * 0.5e0, y => 2e0);
        $s += $p.len2 + $p.shift(1).x;
        my $q = Point.new(x => $i / 4, y => 1);
        $r += $q.x + $q.shift(1).y;
    }
    "$s $r"
}
sub defaults() {
    my $s = 0; my $f = 0e0;
    for 1 .. 100 -> $i { my $d = Dflt.new(a => $i); $s += $d.sum + $d.w; $f += $d.fl }
    my $u = 0;
    for 1 .. 100 -> $i { my $d = Dflt.new(a => $i, w => 1, f => 2e0); $u += $d.sum }
    "$s $f $u"
}
sub strs() {
    my $t = ''; my $n = 0;
    for 1 .. 30 -> $i {
        my $p = Named.new(name => 'p' ~ $i, n => $i);
        $t ~= $p.name;
        $n += $p.len + $p.n;
    }
    my $u = '';
    for 1 .. 5 -> $i { my $p = Named.new(name => 'q', n => $i); $u ~= $p.tag ~ ' ' }
    "$t $n $u"
}
sub overridden() {
    my $s = 0;
    for 1 .. 50 -> $i { my $p = Over.new(x => $i); $s += $p.x + $p.get }
    $s
}
sub never-given() {
    my $s = 0; my $u = 0;
    for 1 .. 20 -> $i { my $p = Point.new(x => $i); $s += $p.x; $u++ unless $p.y.defined }
    my $z = 0;
    for 1 .. 20 -> $i { my $p = Priv.new(a => $i, z => 5); $z += $p.a; $z++ unless $p.z.defined }
    "$s $u $z"
}
sub not-plain() {
    my $s = 0;
    for 1 .. 50 -> $i {
        my $a = Typed.new(x => $i);  my $b = Built.new(x => $i);
        my $c = Tweak.new(x => $i);  my $d = OwnNew.new($i);
        $s += $a.dbl + $b.get + $c.get + $d.get;
    }
    $s
}
sub not-inlined() {
    my $s = 0;
    for 1 .. 40 -> $i {
        my $r = Rec.new(n => $i % 5);  my $t = Stmts.new(x => $i);  my $f = Free.new(x => $i);
        $s += $r.down + $t.m + $f.m;
    }
    my $k = 0;
    for 1 .. 40 -> $i { my $p = Kid2.new(x => $i, y => 1); $k += $p.len2 }
    my $e = 0;
    for 1 .. 10 -> $i { my $p = Fx.new(v => $i); $p.bump; $e += $p.bump + $p.v }
    "$s $k $e"
}
sub inherited() {
    my $s = 0; my $b = 0;
    for 1 .. 60 -> $i {
        my $k = Kid.new(x => $i, y => 2, z => 3);
        $s += $k.vol + $k.len2 + $k.shift(1).x;
        my $x = Box.new(w => $i, size => $i % 20);
        $b += $x.area;  $b++ if $x.big;
    }
    my $e = 0;
    for 1 .. 10 -> $i { my $p = Elems.new(elems => $i, keys => 2); $e += $p.elems + $p.keys + $p.both }
    "$s $b $e"
}
sub escapes() {
    my @keep; my $s = 0;
    for 1 .. 10 -> $i { my $p = Point.new(x => $i, y => 0); @keep.push($p); $s += $p.x }
    my $last;
    for 1 .. 10 -> $i { my $p = Point.new(x => $i, y => 1); $last = $p }
    my $str = '';
    for 1 .. 3 -> $i { my $p = Point.new(x => $i, y => 1); $str ~= $p.gist }
    my $w = 0;
    for 1 .. 10 -> $i { my $p = Point.new(x => $i, y => 1); my $q = $p; $w += $q.x + ($p === $q ?? 1 !! 0) }
    "{@keep.map(*.x).sum} $s {$last.x} $str $w"
}
sub outgrows() {
    my $s = 0;
    for 1 .. 70 -> $i { my $p = Point.new(x => 2 ** $i, y => 3); $s += $p.len2 }
    my $t = 0;
    for 1 .. 5 -> $i { my $p = Point.new(x => 4611686018427387904, y => $i); $t += $p.shift(4611686018427387904).x }
    "$s $t"
}
sub odd-args() {
    my $s = 0;
    for 1 .. 10 -> $i { my $p = Point.new(x => $i, y => 1, nope => $i * 2); $s += $p.x }
    for 1 .. 10 -> $i { my $p = Point.new(x => $i, x => 5, y => 1); $s += $p.x }
    for 1 .. 10 -> $i { my $p = Point.new(:x($i), :y(2)); $s += $p.len2 }
    for 1 .. 10 -> $i { my $x = $i; my $y = 3; my $p = Point.new(:$x, :$y); $s += $p.len2 }
    my $r = (try { my $t = 0; for 1 .. 3 -> $i { my $p = Point.new($i, 2); $t += $p.x }; $t }) // "refused: {$!.^name}";
    my $m = (try { my $t = 0; for 1 .. 3 -> $i { my $p = Point.new(x => $i, y => 2); $t += $p.scaled(1.5).x }; $t })
            // "refused: {$!.^name}";
    my $n = (try { my $t = 0; for 1 .. 3 -> $i { my $p = Point.new(x => $i, y => 2); $t += $p.nosuch }; $t })
            // "refused: {$!.^name}";
    "$s $r $m $n"
}
sub obj-args() {
    my $s = 0;
    for 1 .. 50 -> $i {
        my $a = Vec.new(a => $i, b => 1);  my $b = Vec.new(a => 2, b => $i);
        $s += $a.add($b).sum + $a.dot($b) + $a.add($a.add($b)).dot(Vec.new(a => 1, b => 1)) + $a.same($b).sum;
    }
    my $r = (try { my $t = 0; for 1 .. 3 -> $i { my $a = Vec.new(a => $i, b => 1); $t += $a.dot(Point.new(x => 1, y => 2)) }; $t })
            // "refused: {$!.^name}";
    "$s $r"
}
# objects made before the loop and only read in it
class Own    { has $.v; has $.w; method new($a) { self.bless(v => $a, w => 0.5e0) }; method twice() { $.v * 2 + $.w } }
class Setter { has $.x; method set($v) { $!x = $v }; method name() { 'pt' ~ $.x } }
sub reads($p) { my $s = 0; for 1 .. 10 -> $i { $s += $p.len2 + $p.x }; $s }
sub outside() {
    my $p = Point.new(x => 3, y => 4);  my $q = Point.new(x => 1, y => 2);  my $v = Vec.new(a => 5, b => 6);
    my $acc = 0;
    for 1 .. 500 -> $i { $acc += $p.len2 + $p.x + $p.shift($i).len2 + $v.dot(Vec.new(a => $i, b => 1)) + $v.add($v).sum }
    my $o = Own.new(7);  my $t = Tweak.new(x => 1);  my $y = Typed.new(x => 5);  my $u = 0;
    for 1 .. 50 -> $i { $u += $o.twice + $t.get + $y.dbl }
    my $w = Setter.new(x => 3);  my $z = 0;  my $n = '';  my @k;
    for 1 .. 10 -> $i { $w.set($i); $z += $w.x }
    for 1 .. 3 -> $i { $n ~= $w.name; @k.push($w) }
    my $r = Point.new(x => 2, y => 4);  my $h = 0;
    for 1 .. 5 -> $i { $h += $r.x; $r = $r.shift(1) }
    my $big = Point.new(x => 2 ** 62, y => 4);  my $b = 0;
    for 1 .. 5 -> $i { $b += $big.len2 }
    my $half = Point.new(x => 2);  my $d = 0;
    for 1 .. 5 -> $i { $d += $half.x; $d++ unless $half.y.defined }
    "$acc $u $z {$w.x} $n {+@k} $h {$r.x} $b $d"
}
# …the same loop given an object of other attribute types, of another class, and no object
sub outside-kinds() {
    (reads(Point.new(x => 1, y => 2)), reads(Point.new(x => 1.5e0, y => 2)), reads(Point.new(x => 3, y => 2/3)),
     reads(Kid.new(x => 1, y => 2, z => 3)), reads(Point.new(x => 4, y => 5)),
     (try reads(42)) // "refused: {$!.^name}", (try reads(Own.new(1))) // "refused: {$!.^name}").join(' ')
}
# the same loop entered again after what it was compiled against has changed
sub again($k) {
    my $s = 0;
    for 1 .. 20 -> $i { my $p = Wrapped.new(x => $i + $k); $s += $p.val }
    $s
}
sub shadowed() {
    my class Local { has $.x; has $.y; method len2() { $.x - $.y } }
    my $s = 0;
    for 1 .. 10 -> $i { my $p = Local.new(x => $i, y => 2); $s += $p.len2 }
    $s
}
my $late = 0;
sub late-class() {
    my $s = 0;
    for 1 .. 3 -> $round {
        for 1 .. 10 -> $i { my $p = Point.new(x => $i, y => $round); $s += $p.len2 }
        EVAL "class Late{++$late} \{ has \$.q \}" if $round == 1;
    }
    $s
}

sub assigns() {
    my $a = 0;
    for 1 .. 60 -> $i { my $p = Step.new(x => $i, y => 1); $p = $p.shift(2); $p = $p.swap if $i %% 3; $a += $p.len2 + $p.x }
    my $w = 0;
    for 1 .. 30 -> $i { my $p = Step.new(x => $i, y => 0); my $n = 0; while $p.x < 200 { $p = $p.shift(37); $n++ }; $w += $n + $p.x }
    my $m = 0;
    for 1 .. 30 -> $i { my $p = Step.new(x => $i, y => 2); $p = $p.me; $p = $p; $p = $p.clamp(9).me; $m += $p.x + $p.y }
    my $x = 0;
    for 1 .. 30 -> $i { my $p = Step.new(x => $i, y => 2); $p = Step.new(x => $p.y, y => $p.x); $x += $p.x * 100 + $p.y }
    "$a $w $m $x"
}
sub aliases() {
    my $a = 0;
    for 1 .. 60 -> $i { my $p = Step.new(x => $i, y => 1); my $q = $p; $p = $p.shift(5); $a += $q.x * 3 + $p.x }
    my $b = 0;
    for 1 .. 60 -> $i { my $p = Step.new(x => $i, y => 1); my $q = $p.me; $q = $q.shift(5); my $r = $q; $q = $q.swap; $b += $q.x * 3 + $p.x + $r.x }
    my $c = 0;
    for 1 .. 30 -> $i { my $p = Step.new(x => $i, y => 1); $c += $p.reassign(Step.new(x => $i * 2, y => 0)) }
    "$a $b $c"
}
sub ternaries() {
    my $a = 0;
    for 1 .. 60 -> $i {
        my $p = $i %% 2 ?? Step.new(x => $i, y => 2) !! Step.new(x => 7, y => $i);
        $a += $p.len2 + ($i %% 5 ?? $p !! $p.swap).x;
    }
    my $b = 0;   # two classes, and two shapes of one: the loop as before
    for 1 .. 30 -> $i { my $p = $i %% 2 ?? Step.new(x => $i, y => 2) !! Other.new(x => 7, y => $i); $b += $p.len2 }
    for 1 .. 30 -> $i { my $p = $i %% 2 ?? Step.new(x => $i, y => 2) !! Step.new(x => 1.5e0, y => $i); $b += $p.len2.Int }
    for 1 .. 30 -> $i { my $p = Step.new(x => $i, y => 2); $p = Step.new(x => 0.5e0, y => 1) if $i %% 4; $b += $p.len2.Int }
    for 1 .. 30 -> $i { my $p = Step.new(x => $i, y => 2); $p = Other.new(x => 0, y => 1) if $i %% 4; $b += $p.len2 }
    for 1 .. 30 -> $i { my $p = Step.new(x => $i, y => 2); $p = $i if $i %% 7; $b += $p ~~ Int ?? 1 !! $p.x }
    "$a $b"
}
sub statements() {
    my $a = 0;
    for 1 .. 80 -> $i {
        my $p = Step.new(x => $i - 40, y => $i % 7 - 3);
        $a += $p.norm1 + $p.sign;
        $a += 1 if $p.big(20);
        $a += $p.clamp(10).x + $p.step($i).len2 % 1000;
        $a += $p.deep($i % 9) + $p.outer2($i % 11);
    }
    my $n = 0e0;
    for 1 .. 40 -> $i { my $p = Step.new(x => $i % 9, y => $i); $n += $p.half }
    "$a $n"
}
sub statement-edges() {
    my @r;
    my $a = 0e0;
    for 1 .. 20 -> $i { my $p = Step.new(x => $i % 6, y => $i); $a += $p.mixed }
    @r.push: $a;
    my $b = 0;
    for 1 .. 20 -> $i { my $p = Step.new(x => $i % 6, y => $i); my $e = $p.either; $b += $e ~~ Step ?? 100 !! $e }
    @r.push: $b;
    my $s = '';
    for 1 .. 5 -> $i { my $p = Step.new(x => $i, y => 0); $s ~= $p.name }
    @r.push: $s;
    my $q = 0;
    for 1 .. 20 -> $i { my $p = Step.new(x => $i, y => 0); $q += $p.rat }
    @r.push: $q;
    my $f = 0;
    for 1 .. 20 -> $i { my $p = Step.new(x => $i, y => 0); $f += $p.frees + $p.unused }
    @r.push: $f;
    my $u = 0;
    for 1 .. 6 -> $i { my $p = Step.new(x => $i, y => 0); my $v = $p.noelse; $u += $v.defined ?? $v !! 1000 }
    @r.push: $u;
    my $g = 0;   # outgrows int64 inside a body of statements: the loop again, the ordinary way
    for 1 .. 12 -> $i { my $p = Step.new(x => 10 ** $i, y => 0); $g += $p.sq }
    @r.push: $g;
    @r.join(' ')
}
my $n-alias = 0;
sub carried() {
    my @r;
    my $p = Out.new(x => 1, y => 2);  my $o = $p;
    for 1 .. 100 -> $i { $p = $p.shift(1) }
    @r.push: "{$p.x} {$p.y} {$p.z} {$p.w.raku} {$o.x} {$p === $o} {$p.WHAT.raku}";
    for 1 .. 100 -> $i { $p = $p.shift(1) if $i > 1000 }   # never assigned: the same object still
    my $same = $p;
    for 1 .. 10 -> $i { $p = $p.shift(1) if $i > 1000 }
    @r.push: $p === $same;
    my $n = 0;
    while $p.y < 140 { $p = $p.step; $n++ }
    @r.push: "$n {$p.x} {$p.y}";
    my $q = Out.new(x => 5, y => 5);
    for 1 .. 50 -> $i { $p = $i %% 2 ?? $p.shift(1) !! $q.shift($i); $q = Out.new(x => $p.y, y => $p.x) }
    @r.push: "{$p.x} {$p.y} {$q.x} {$q.y}";
    my Out $t = Out.new(x => 1, y => 2);   # a typed variable, a Rat, a Num
    for 1 .. 70 -> $i { $t = $t.shift(2) }
    my $f = Out.new(x => 1.5, y => 2e0);
    for 1 .. 9 -> $i { $f = $f.shift(1) }
    @r.push: "{$t.x} {$f.x} {$f.x.WHAT.raku} {$f.y}";
    @r.join(' | ')
}
sub carried-edges() {
    my @r;
    # an object with a name: the variable must hold that very object
    my $p = Out.new(x => 1, y => 2);  my $o = $p;
    for 1 .. 20 -> $i { $p = $p.me }
    @r.push: $p === $o;
    my $k;
    for 1 .. 20 -> $i { my $t = $p.shift(1); $p = $t; $k = $t if $i == 20 }
    @r.push: "{$p.x} {$p === $k}";
    # another class, another attribute type, a Str: the loop as before
    for 1 .. 5 -> $i { $p = Other.new(x => $i, y => 0) }
    @r.push: $p.WHAT.raku ~ $p.x;
    $p = Out.new(x => 1, y => 2);
    for 1 .. 5 -> $i { $p = Out.new(x => $p.x + 0.5e0, y => 1) }
    @r.push: $p.x;
    for 1 .. 5 -> $i { $p = $p.named("s$i") }
    @r.push: $p.y;
    # outgrows int64: again the ordinary way, the variable untouched until then
    $p = Out.new(x => 1, y => 2);
    for 1 .. 70 -> $i { $p = Out.new(x => $p.x * 3, y => 1) }
    @r.push: $p.x;
    # two names for one container; a container that is not a plain one
    my $a = Out.new(x => 1, y => 1);  my $b := $a;
    for 1 .. 10 -> $i { $a = $a.shift(1); $n-alias += $b.x }
    @r.push: "{$a.x} {$b.x} $n-alias";
    my $ro := Out.new(x => 1, y => 1);
    try { for 1 .. 10 -> $i { $ro = $ro.shift(1) } }
    @r.push: "{$ro.x} {$!.defined}";
    @r.join(' | ')
}
sub pushed() {
    my @r;
    my @a;
    for 0 .. 49 -> $i { @a.push(Out.new(x => $i, y => $i * 2)) }
    for 50 .. 60 -> $i { @a.push: Out.new(x => $i, y => 1).shift(1) }
    @r.push: "{+@a} {@a[7].x} {@a[55].y} {@a[3].z} {@a[3].w.raku} {@a[0].WHAT.raku} {@a.map(*.x).sum}";
    # a name pushed twice is one object; a copy of it is that object too
    my @b; my $s = 0;
    for 0 .. 49 -> $i { my $p = Out.new(x => $i, y => 1); $s += $p.x; @b.push($p); @b.push($p) if $i %% 10; my $q = $p; @b.push($q) if $i == 3 }
    @r.push: "$s {+@b} {@b[0] === @b[1]} {@b[1] === @b[2]} {@b.grep({ $_ === @b[4] }).elems} {@b[*-1].x}";
    # a name assigned on the way; the array used as an array afterwards
    my @c; my $n = 0;
    for 0 .. 39 -> $i { my $p = Out.new(x => $i, y => 1); $p = $p.shift(100) if $i %% 2; @c.push($p); $n += @c.elems }
    @c[0] = 5; @c.push(7);
    @r.push: "$n {+@c} {@c[2].x} {@c[1].x} {@c[*-1]} {@c[0]}";
    # outgrows int64 after objects were pushed: the pushes taken back, the loop again
    my @d;
    for 0 .. 69 -> $i { my $p = Out.new(x => 3 ** $i, y => 1); @d.push($p); my $v = $p.x * $p.x }
    @r.push: "{+@d} {@d[*-1].x}";
    # Rats and Nums; an array of Ints, a Str attribute, an Int pushed after: as before
    my @e; my @f = 1, 2;
    for 1 .. 5 -> $i { @e.push(Out.new(x => 1.5, y => 2e0)); @f.push(Out.new(x => $i, y => 1)) }
    for 1 .. 5 -> $i { @e.push(Out.new(x => "s$i", y => 1)); @e.push($i) }
    @r.push: "{@e[4].x.raku} {@e[4].y.raku} {+@f} {@f[2].x} {+@e} {@e[5].x} {@e[6]}";
    @r.join(' | ')
}
sub kept() {
    my @r;
    # a variable of the program given an object with a name: that very object
    my $p = Out.new(x => 0, y => 0); my @a; my $o = $p;
    for 0 .. 29 -> $i { my $t = $p.shift(1); @a.push($t) if $i %% 7; $p = $t }
    @r.push: "{$p.x} {+@a} {@a[*-1].x} {$p === @a[*-1]} {$o.x} {@a[0] === $o}";
    my $k;
    for 1 .. 20 -> $i { my $t = $p.shift(1); $p = $t; }
    @r.push: $p.x;
    # the variable's own object pushed, before and after it is given another
    my @b;
    for 0 .. 29 -> $i { @b.push($p); $p = $p.shift(2) if $i %% 3 }
    @r.push: "{$p.x} {+@b} {@b[0] === @b[1]} {@b[0] === @b[2]} {@b[*-1] === $p} {@b.map(*.x).sum}";
    # one variable of the program given another's object
    my $q = Out.new(x => 9, y => 9); my $q0 = $q;
    for 0 .. 29 -> $i { $p = $q if $i == 20; my $t = $q.shift(1) }
    @r.push: "{$p.x} {$p === $q} {$q === $q0}";
    @r.join(' | ')
}
sub typed() {
    my @r;
    my $s = 0;
    for 0 .. 99 -> $i { my $p = Ty.new(x => $i, b => $i %% 2); $s += $p.len2 + $p.shift(3).x + ($p.b ?? 1 !! 0) }
    @r.push: $s;
    my $p = Ty.new(x => 1, b => True);
    for 1 .. 51 -> $i { $p = $p.shift(1) }
    my $q = Ty.new;
    @r.push: "{$p.raku} {$q.raku} {$q.x.raku} {$q.s.raku} {$q.b.raku}";
    my @a;
    for 0 .. 9 -> $i { @a.push(Ty.new(x => $i, f => $i * 1e0)) }
    @r.push: "{+@a} {@a[9].raku}";
    $p.setx(5);
    my $ok = try { $p.setx("a"); 1 };
    @r.push: "{$p.x} {$ok.raku} {$!.^name}";
    @r.join(' | ')
}
sub typed-refused() {
    my @r;
    # what a typed attribute refuses, or takes as something else: as it was
    for (Ty, {x => "a"}), (Ty, {x => 1.5}), (Ty, {x => True}), (Ty, {f => 1}), (Ty, {s => 5}), (Ty, {b => 1}), (Ty, {x => Int}),
        (Ty, {x => Nil}), (Ty, {x => 2**70}), (Ty, {x => <5>}), (Ty, {s => "ok", u => [1]}), (TySm, {x => 3, r => 1.5, c => "7"}),
        (TySu, {x => 3, n => 4}), (TySu, {x => -3}) -> ($c, %a) {
        @r.push: (try $c.new(|%a).raku) // "ERR " ~ $!.^name;
    }
    # …and in a loop: the error where the generic path raises it, after the iterations before it
    my @a; my $n = 0;
    try { for 0 .. 19 -> $i { $n++; my $t = Ty.new(x => $i, f => $i) } }
    @r.push: "$n {$!.^name}";
    try { for 0 .. 19 -> $i { @a.push(Ty.new(x => $i)); my $t = Ty.new(x => $i > 10 ?? 1.5e0 !! 1e0) } }
    @r.push: "{+@a} {$!.^name}";
    @r.join(' | ')
}
sub writes() {
    my @r;
    # an object the loop makes: `$!x = …`, `++`, `+=`, an `is rw` accessor, a method that hands itself back
    my $a = 0;
    for 0 .. 99 -> $i {
        my $c = Wr.new(n => $i); $c.bump; $c.inc; $c.inc; $a += $c.bump + $c.t + $c.n;
        $c.n = $c.n * 2; $c.n += 3; $a += $c.n; $c.add(5).add(1); $a += $c.total; $a += 1 if $c.scale(2) > 1e0;
    }
    @r.push: $a;
    # an object of the program: its own attributes when the loop ends, and it is the same object
    my $c = Wr.new(n => 0); my $o = $c;
    for 0 .. 999 -> $i { $c.bump; $c.inc if $i %% 3; $c.add($i); $c.n = $c.n - 1 if $i %% 7 }
    @r.push: "{$c.n} {$c.t} {$o.n} {$o === $c}";
    # one object's method writing another's
    my $x = Wr.new(n => 1); my $y = Wr.new(n => 2);
    for 1 .. 20 -> $i { $x.both($y); $y.both($x) }
    @r.push: "{$x.n} {$y.n}";
    @r.join(' | ')
}
# a statement that answers an object nobody takes is still sunk
my $sunk = 0;
class Sunk { has $.v; method sink() { $sunk++ }; method me() { self } }
sub sinks() {
    for 0 .. 4 -> $i { Sunk.new(v => $i); my $s = Sunk.new(v => 1); $s.me; }
    my $t = 0;
    for 0 .. 4 -> $i { my $q = Wr.new(n => 0); $q.add($i); Wr.new(n => 2); $t += $q.n }
    "$sunk $t"
}
sub writes-refused() {
    my @r;
    # two names for one object: a write through one is read through the other
    my $a = 0;
    for 0 .. 49 -> $i { my $p = Wr.new(n => $i); my $q = $p; $p.bump; $a += $q.n * 100 + $p.n }
    for 0 .. 49 -> $i { my $p = Wr.new(n => $i); my $q = $p.me; $q.n = 7; $a += $p.n }
    for 0 .. 49 -> $i { my $p = Wr.new(n => $i); my $q = Wr.new(n => 1); my $t = $i %% 2 ?? $p !! $q; $t.bump; $a += $p.n * 10 + $q.n }
    for 0 .. 49 -> $i { my $p = Wr.new(n => $i); $p.bump; my $q = $p; $q.bump; $a += $p.n }
    @r.push: $a;
    my $p = Wr.new(n => 1); my $q = $p; my $b = 0;
    for 1 .. 20 -> $i { $p.bump; $b += $q.n }
    @r.push: "$b {$p.n}";
    # an object that leaves the loop, an accessor that is not rw, an attribute never given,
    # a type that would change, a number that outgrows int64, a variable also replaced
    my $c = Wr.new(n => 0, ro => 1); my @l;
    for 0 .. 19 -> $i { $c.bump; @l.push($c) if $i == 5 }
    my $z = try { for 1 .. 3 { $c.ro = 5 }; 1 };
    my $d = Wr.new(n => 9007199254740993);
    for 0 .. 69 -> $i { $d.n = $d.n * 3 if $i > 40; $d.inc }
    my $g = Wr.new(n => 1);
    for 0 .. 4 -> $i { $g.tag("x$i"); $g.n = $g.n + 0.5 }
    @r.push: "{$c.n} {@l[0].n} {$z.raku} {$d.n} {$d.t} {$g.s} {$g.n}";
    my $e = Wr.new(n => 0);
    for 0 .. 29 -> $i { $e = $e.add(1) if $i > 100; $e.bump; my $k = Wr.new(n => $i); $k.reset; $e.n += $k.total }
    my @s = 1, 2, 3; my $f = 0;
    for 0 .. 2 -> $i { $f += @s[$e.t]; $e.inc }
    @r.push: "{$e.n} {$e.t} $f";
    @r.join(' | ')
}
for ^3 {
    o 'basic ', basic();
    o 'accessors ', accessors();
    o 'chains ', chains();
    o 'whiles ', whiles();
    o 'nums ', nums();
    o 'defaults ', defaults();
    o 'strs ', strs();
    o 'overridden ', overridden();
    o 'never-given ', never-given();
    o 'not-plain ', not-plain();
    o 'not-inlined ', not-inlined();
    o 'inherited ', inherited();
    o 'escapes ', escapes();
    o 'outgrows ', outgrows();
    o 'odd-args ', odd-args();
    o 'obj-args ', obj-args();
    o 'outside ', outside();
    o 'outside-kinds ', outside-kinds();
    o 'shadowed ', shadowed();
    o 'late-class ', late-class();
    o 'assigns ', assigns();
    o 'aliases ', aliases();
    o 'ternaries ', ternaries();
    o 'statements ', statements();
    o 'statement-edges ', statement-edges();
    o 'carried ', carried();
    o 'carried-edges ', carried-edges();
    o 'pushed ', pushed();
    o 'kept ', kept();
    o 'typed ', typed();
    o 'typed-refused ', typed-refused();
    o 'writes ', writes();
    o 'writes-refused ', writes-refused();
    o 'sinks ', sinks();
}
o 'again ', again(0), ' ', again(1);
# (Rakudo 2026.09 answers 230 for the wrapped call: it runs the method unwrapped there)
my $h = Wrapped.^find_method('val').wrap(-> | { 1000 });
o 'wrapped ', again(0);
Wrapped.^find_method('val').unwrap($h);
o 'unwrapped ', again(0);
{
    my $t = 0;
    my @th = (1 .. 4).map: -> $n { start { my $s = 0; for 1 .. 500 -> $i { my $p = Point.new(x => $i, y => $n); $s += $p.len2 }; $s } };
    o 'threads ', await(@th).join(' ');
}

my @want = q:to/WANT-END/.lines;
basic 7572195
accessors 125250 627252000
chains 18249250
whiles 42676700
nums 682725 5025
defaults 6450 300 5150
strs p1p2p3p4p5p6p7p8p9p10p11p12p13p14p15p16p17p18p19p20p21p22p23p24p25p26p27p28p29p30 1011 q#1 q#2 q#3 q#4 q#5 
overridden 5475
never-given 210 20 230
not-plain 17900
not-inlined 6580 22220 150
inherited 86920 18837 150
escapes 55 55 10 Point.new(x => 1, y => 1)Point.new(x => 2, y => 1)Point.new(x => 3, y => 1) 65
outgrows 1858395433210885261794643189387363458832330 46116860184273879040
odd-args 1005 refused: X::Constructor::Positional refused: X::TypeCheck::Binding::Parameter refused: X::Method::NotFound
obj-args 11925 refused: X::TypeCheck::Binding::Parameter
outside 83999750 1325 55 10 pt10pt10pt10 3 20 7 106338239662793269832304564822427566160 15
outside-kinds 60 77.5 124.444444 60 450 refused: X::Method::NotFound refused: X::Method::NotFound
shadowed 35
late-class 1295
assigns 83250 6697 294 6465
aliases 7620 5040 1890
ternaries 76420 29607
statements 91810 452
statement-edges 156 747 n2n3n4n5n6 76.666667 2420 3015 1000100010001000100010001000100010001000100010000
carried 101 102 5 Any 1 False Out | True | 334 0 140 | 655 655 655 655 | 141 10.5 Rat 11
carried-edges True | 21 True | Other5 | 3.5 | s5 | 2503155504993241601315571986085849 | 11 11 65 | 1 True
pushed 61 7 2 5 Any Out 1841 | 1225 56 True False 2 49 | 820 41 102 1 7 5 | 70 834385168331080533771857328695283 | 1.5 2e0 7 1 15 s1 1
kept 30 5 29 False 0 False | 50 | 70 30 False False True 1810 | 9 True True
typed 334350 | Ty.new(x => 52, f => 3377699720527872e0, s => Str, b => Bool::False, d => 7, u => Any) Ty.new(x => Int, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) Int Str Bool | 10 Ty.new(x => 9, f => 9e0, s => Str, b => Bool, d => 7, u => Any) | 5 Any X::TypeCheck::Assignment
typed-refused ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | Ty.new(x => Bool::True, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | Ty.new(x => Int, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | ERR X::TypeCheck::Assignment | Ty.new(x => 1180591620717411303424, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | Ty.new(x => IntStr.new(5, "5"), f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | Ty.new(x => Int, f => 1.5e0, s => "ok", b => Bool, d => 7, u => $[1]) | TySm.new(x => 3, r => 1.5, c => 7) | TySu.new(x => 3, n => 4) | ERR X::TypeCheck::Assignment | 1 X::TypeCheck::Assignment | 1 X::TypeCheck::Assignment
writes 32800 | 500357 1334 500357 True | 370248451 228826127
writes-refused 143025 | 230 21 | 20 20 Any 618167483853567272612018748819 70 x4 3.5 | 30 3 6
sinks 10 10
basic 7572195
accessors 125250 627252000
chains 18249250
whiles 42676700
nums 682725 5025
defaults 6450 300 5150
strs p1p2p3p4p5p6p7p8p9p10p11p12p13p14p15p16p17p18p19p20p21p22p23p24p25p26p27p28p29p30 1011 q#1 q#2 q#3 q#4 q#5 
overridden 5475
never-given 210 20 230
not-plain 17900
not-inlined 6580 22220 150
inherited 86920 18837 150
escapes 55 55 10 Point.new(x => 1, y => 1)Point.new(x => 2, y => 1)Point.new(x => 3, y => 1) 65
outgrows 1858395433210885261794643189387363458832330 46116860184273879040
odd-args 1005 refused: X::Constructor::Positional refused: X::TypeCheck::Binding::Parameter refused: X::Method::NotFound
obj-args 11925 refused: X::TypeCheck::Binding::Parameter
outside 83999750 1325 55 10 pt10pt10pt10 3 20 7 106338239662793269832304564822427566160 15
outside-kinds 60 77.5 124.444444 60 450 refused: X::Method::NotFound refused: X::Method::NotFound
shadowed 35
late-class 1295
assigns 83250 6697 294 6465
aliases 7620 5040 1890
ternaries 76420 29607
statements 91810 452
statement-edges 156 747 n2n3n4n5n6 76.666667 2420 3015 1000100010001000100010001000100010001000100010000
carried 101 102 5 Any 1 False Out | True | 334 0 140 | 655 655 655 655 | 141 10.5 Rat 11
carried-edges True | 21 True | Other5 | 3.5 | s5 | 2503155504993241601315571986085849 | 11 11 130 | 1 True
pushed 61 7 2 5 Any Out 1841 | 1225 56 True False 2 49 | 820 41 102 1 7 5 | 70 834385168331080533771857328695283 | 1.5 2e0 7 1 15 s1 1
kept 30 5 29 False 0 False | 50 | 70 30 False False True 1810 | 9 True True
typed 334350 | Ty.new(x => 52, f => 3377699720527872e0, s => Str, b => Bool::False, d => 7, u => Any) Ty.new(x => Int, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) Int Str Bool | 10 Ty.new(x => 9, f => 9e0, s => Str, b => Bool, d => 7, u => Any) | 5 Any X::TypeCheck::Assignment
typed-refused ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | Ty.new(x => Bool::True, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | Ty.new(x => Int, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | ERR X::TypeCheck::Assignment | Ty.new(x => 1180591620717411303424, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | Ty.new(x => IntStr.new(5, "5"), f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | Ty.new(x => Int, f => 1.5e0, s => "ok", b => Bool, d => 7, u => $[1]) | TySm.new(x => 3, r => 1.5, c => 7) | TySu.new(x => 3, n => 4) | ERR X::TypeCheck::Assignment | 1 X::TypeCheck::Assignment | 1 X::TypeCheck::Assignment
writes 32800 | 500357 1334 500357 True | 370248451 228826127
writes-refused 143025 | 230 21 | 20 20 Any 618167483853567272612018748819 70 x4 3.5 | 30 3 6
sinks 20 10
basic 7572195
accessors 125250 627252000
chains 18249250
whiles 42676700
nums 682725 5025
defaults 6450 300 5150
strs p1p2p3p4p5p6p7p8p9p10p11p12p13p14p15p16p17p18p19p20p21p22p23p24p25p26p27p28p29p30 1011 q#1 q#2 q#3 q#4 q#5 
overridden 5475
never-given 210 20 230
not-plain 17900
not-inlined 6580 22220 150
inherited 86920 18837 150
escapes 55 55 10 Point.new(x => 1, y => 1)Point.new(x => 2, y => 1)Point.new(x => 3, y => 1) 65
outgrows 1858395433210885261794643189387363458832330 46116860184273879040
odd-args 1005 refused: X::Constructor::Positional refused: X::TypeCheck::Binding::Parameter refused: X::Method::NotFound
obj-args 11925 refused: X::TypeCheck::Binding::Parameter
outside 83999750 1325 55 10 pt10pt10pt10 3 20 7 106338239662793269832304564822427566160 15
outside-kinds 60 77.5 124.444444 60 450 refused: X::Method::NotFound refused: X::Method::NotFound
shadowed 35
late-class 1295
assigns 83250 6697 294 6465
aliases 7620 5040 1890
ternaries 76420 29607
statements 91810 452
statement-edges 156 747 n2n3n4n5n6 76.666667 2420 3015 1000100010001000100010001000100010001000100010000
carried 101 102 5 Any 1 False Out | True | 334 0 140 | 655 655 655 655 | 141 10.5 Rat 11
carried-edges True | 21 True | Other5 | 3.5 | s5 | 2503155504993241601315571986085849 | 11 11 195 | 1 True
pushed 61 7 2 5 Any Out 1841 | 1225 56 True False 2 49 | 820 41 102 1 7 5 | 70 834385168331080533771857328695283 | 1.5 2e0 7 1 15 s1 1
kept 30 5 29 False 0 False | 50 | 70 30 False False True 1810 | 9 True True
typed 334350 | Ty.new(x => 52, f => 3377699720527872e0, s => Str, b => Bool::False, d => 7, u => Any) Ty.new(x => Int, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) Int Str Bool | 10 Ty.new(x => 9, f => 9e0, s => Str, b => Bool, d => 7, u => Any) | 5 Any X::TypeCheck::Assignment
typed-refused ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | Ty.new(x => Bool::True, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | ERR X::TypeCheck::Assignment | Ty.new(x => Int, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | ERR X::TypeCheck::Assignment | Ty.new(x => 1180591620717411303424, f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | Ty.new(x => IntStr.new(5, "5"), f => 1.5e0, s => Str, b => Bool, d => 7, u => Any) | Ty.new(x => Int, f => 1.5e0, s => "ok", b => Bool, d => 7, u => $[1]) | TySm.new(x => 3, r => 1.5, c => 7) | TySu.new(x => 3, n => 4) | ERR X::TypeCheck::Assignment | 1 X::TypeCheck::Assignment | 1 X::TypeCheck::Assignment
writes 32800 | 500357 1334 500357 True | 370248451 228826127
writes-refused 143025 | 230 21 | 20 20 Any 618167483853567272612018748819 70 x4 3.5 | 30 3 6
sinks 30 10
again 230 250
wrapped 20000
unwrapped 230
threads 41792250 41793750 41796250 41799750
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
