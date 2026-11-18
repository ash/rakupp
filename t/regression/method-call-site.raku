# Regression guard: `$obj.name(…)` answered by what the call site has learned
# of the invocant's class.
#
# A method call on a user object walked methodCall's ladder — some sixty tests
# of the name and the invocant's kind, written for every built-in type — then
# looked the name up through the ancestry (methodCallOwn) and asked whether an
# ancestor or a built-in stands behind the method (invokeMethodChain): about
# 900 instructions before the method's own frame. None of those answers
# depends on the object, so Interpreter::siteMethodCall (SiteCache.cpp) keeps
# them on the MethodCall node, per class and symbol generation, up to four
# classes a site. What must stay true: the call reaches what it reached —
# an inherited or role method, `callsame`/`nextsame`/`callwith` through the
# ancestry, a multi, a private method, FALLBACK, a delegated name, a built-in
# behind a user method, a junction argument autothreading, a Seq answered
# afresh, an `is rw` method assigned through — at a site that sees one class,
# several, and more than the cache holds; and a method added or mixed in
# after the site has run is found.

my @got;
sub o(**@a) { @got.push: @a.map(*.gist).join }
class A {
    has $.x = 1; has @.l = 1, 2; has $.r is rw = 0;
    method plain($a) { $a + $!x }
    method list { (1, 2, 3) }
    method seqy { (1..3).map(* + 1) }
    method me { self }
    method opt($a?, :$n) { ($a // 'none') ~ ($n // '') }
    method slurp(*@a) { @a.elems }
    method cs { 'A.cs' }
    method Str { 'A-str' }
    method gist { 'A-gist' }
    method rwm is rw { $!r }
    method junc($v) { $v ~~ Junction ?? 'J' !! "v$v" }
    method muj(Mu $v) { $v.WHAT.raku }
    method fail-it { fail 'bad' }
    method die-it { die 'dead' }
    method !priv { 'priv' }
    method pub { self!priv }
    multi method mm(Int $i) { 'int' }
    multi method mm(Str $s) { 'str' }
    method via-self { self.x }
    method clone-me { self.clone }
}
class B is A {
    has $.x = 2;
    method cs { 'B.cs>' ~ callsame() }
    method plain($a) { 'B' ~ nextsame() }
    method seqy { callsame().map(* * 10) }
    method clone-me { callsame() }                  # a built-in stands behind A's: Mu.clone
}
class C is B { method cs { 'C.cs>' ~ callwith() } }
role R { method rm { 'rm' ~ self.x }; method cs { 'R.cs' } }
class D is A does R { }
class E { has $.del handles <uc lc> = 'Str'; method FALLBACK($n, |c) { "fb:$n" } }
class F { method x { 'Fx' } }; class G { method x { 'Gx' } }; class H { method x { 'Hx' } }
class I { method x { 'Ix' } }; class J { method x { 'Jx' } }
class K { has $.v; method Bool { $!v > 0 }; method elems { 42 } }
my @objs = A.new, B.new, C.new, D.new;
for ^3 -> $round {
    for @objs -> $o {
        o $o.^name, ': ', $o.plain(10), ' ', $o.list.raku, ' ', $o.seqy.raku, ' ', $o.me.x, ' ', $o.opt, $o.opt(1, :n<z>),
          ' ', $o.slurp(1, 2, 3), ' ', $o.cs, ' ', $o.Str, ' ', $o.gist, ' ', $o.x, ' ', $o.l.raku, ' ', $o.junc(1|2).raku,
          ' ', $o.muj(1|2), ' ', $o.pub, ' ', $o.mm(1), $o.mm('s'), ' ', $o.via-self, ' ', $o.clone-me.x;
        $o.rwm = $round;  $o.r++;  o $o.r;
        o ($o.fail-it // 'failed');
        o (try $o.die-it) // "died: $!";
        o (try $o.nosuch) // "nosuch: {$!.^name}";
        o $o.?maybe.raku;
        my $s = $o.seqy;  o $s.WHAT.raku, ' ', $s.elems;      # each call its own Seq
    }
    o D.new.rm;
    o E.new.uc, E.new.lc, E.new.whatever;
    o (F.new, G.new, H.new, I.new, J.new, F.new).map({ .x }).join(',');           # five classes at one site
    o (F.new, G.new, H.new, I.new, J.new, A.new).map({ $_.x }).join(',');         # …a method, then an accessor
    o ?K.new(v => 1), ?K.new(v => 0), K.new(v => 1).elems;
    my $m = A.new but role { method plain($a) { 'mixed' } };
    o $m.plain(1), $m.cs;
}
# the tables change under a site that has run
my $a = A.new;
sub call-cs($obj) { $obj.cs }
o call-cs($a), ' ', call-cs(B.new);
sub call-late($obj) { (try $obj.late) // 'no such method' }
o call-late($a);
A.^add_method('late', method { 'late' });
o call-late($a), ' ', call-late(B.new);
# an accessor's attribute, read and written between calls
my $h = A.new;
for ^3 { o $h.r;  $h.r = $_ + 1 }
o $h.l.elems, ' ', A.new(x => (1, 2)).x.elems;

my @want = q:to/WANT-END/.lines;
A: 11 (1, 2, 3) (2, 3, 4).Seq 1 none1z 3 A.cs A-str A-gist 1 [1, 2] any("v1", "v2") Junction priv intstr 1 1
1
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
B: 11 (1, 2, 3) (20, 30, 40).Seq 2 none1z 3 B.cs>A.cs A-str A-gist 2 [1, 2] any("v1", "v2") Junction priv intstr 2 2
1
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
C: 11 (1, 2, 3) (20, 30, 40).Seq 2 none1z 3 C.cs>B.cs>A.cs A-str A-gist 2 [1, 2] any("v1", "v2") Junction priv intstr 2 2
1
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
D: 11 (1, 2, 3) (2, 3, 4).Seq 1 none1z 3 R.cs A-str A-gist 1 [1, 2] any("v1", "v2") Junction priv intstr 1 1
1
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
rm1
STRstrfb:whatever
Fx,Gx,Hx,Ix,Jx,Fx
Fx,Gx,Hx,Ix,Jx,1
TrueFalse42
mixedA.cs
A: 11 (1, 2, 3) (2, 3, 4).Seq 1 none1z 3 A.cs A-str A-gist 1 [1, 2] any("v1", "v2") Junction priv intstr 1 1
2
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
B: 11 (1, 2, 3) (20, 30, 40).Seq 2 none1z 3 B.cs>A.cs A-str A-gist 2 [1, 2] any("v1", "v2") Junction priv intstr 2 2
2
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
C: 11 (1, 2, 3) (20, 30, 40).Seq 2 none1z 3 C.cs>B.cs>A.cs A-str A-gist 2 [1, 2] any("v1", "v2") Junction priv intstr 2 2
2
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
D: 11 (1, 2, 3) (2, 3, 4).Seq 1 none1z 3 R.cs A-str A-gist 1 [1, 2] any("v1", "v2") Junction priv intstr 1 1
2
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
rm1
STRstrfb:whatever
Fx,Gx,Hx,Ix,Jx,Fx
Fx,Gx,Hx,Ix,Jx,1
TrueFalse42
mixedA.cs
A: 11 (1, 2, 3) (2, 3, 4).Seq 1 none1z 3 A.cs A-str A-gist 1 [1, 2] any("v1", "v2") Junction priv intstr 1 1
3
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
B: 11 (1, 2, 3) (20, 30, 40).Seq 2 none1z 3 B.cs>A.cs A-str A-gist 2 [1, 2] any("v1", "v2") Junction priv intstr 2 2
3
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
C: 11 (1, 2, 3) (20, 30, 40).Seq 2 none1z 3 C.cs>B.cs>A.cs A-str A-gist 2 [1, 2] any("v1", "v2") Junction priv intstr 2 2
3
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
D: 11 (1, 2, 3) (2, 3, 4).Seq 1 none1z 3 R.cs A-str A-gist 1 [1, 2] any("v1", "v2") Junction priv intstr 1 1
3
failed
died: dead
nosuch: X::Method::NotFound
Nil
Seq 3
rm1
STRstrfb:whatever
Fx,Gx,Hx,Ix,Jx,Fx
Fx,Gx,Hx,Ix,Jx,1
TrueFalse42
mixedA.cs
A.cs B.cs>A.cs
no such method
late late
0
1
2
2 2
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
