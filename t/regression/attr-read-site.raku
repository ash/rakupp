# Regression guard: `$!x` and `$.x` read from their slot by what the node has
# learned of the class.
#
# evalVarExpr's attribute arm asked, for every read in a method body: is
# `self` an object, is its class a C struct, does a method of the name exist
# (`$.x` is `self.x`), has the attribute a sigil twin, does another class of
# the chain declare the name — and then hashed the name. About 700
# instructions a read; `$.x * $.x + $.y * $.y` was four of them.
# Interpreter::attrReadLane (SiteCache.cpp) keeps the class's two answers at
# the node, per class and symbol generation, with the name's hash. What must
# stay true: `$.x` still calls a method of that name where one exists (the
# class's own, an inherited one, one added later), a subclass that declares
# the name again reads its own slot and the parent's methods the parent's,
# one method body serves several classes, `$.list` is an item and `@.list` is
# not, and a read-only `$.x` cannot be assigned through.

my @got;
sub o(**@a) { @got.push: @a.map(*.gist).join }
class A {
    has $.x; has $!p; has @.l; has %.h; has $.rw is rw;
    method setp($v) { $!p = $v; self };
    method r1 { $!x };      method r2 { $.x };     method r3 { $!p };    method r4 { $.l };
    method r5 { @.l };      method r6 { $.h };     method r7 { $!rw };   method r8 { $!x + $.x };
    method assign { $.x = 5 };
    method who { $.who2 };  method who2 { 'method' };
    method flat { flat 0, $.l };
    method both { ($!x, $.x, $!p) }
    method late-read { $.late }
}
class B is A { has $.x; method bx { $!x };  method bx2 { $.x };  method x { 'overridden' } }
class C is A { method r2b { $.x };  has $.y = [1, 2]; method yy { $.y };  method yl { flat 0, $.y } }
class D is A { has $.x }                                   # the same name again: its own slot
role R { has $.rr = 3;  method rread { $!rr + $.rr } }
class E does R { }
class F does R { has $.other = 1 }
my $a = A.new(x => 1, l => (1, 2), h => {a => 1}, rw => 9).setp(7);
for ^3 {
    o $a.r1, $a.r2, $a.r3, $a.r4.raku, $a.r5.raku, $a.r6.raku, $a.r7, $a.r8, $a.who, $a.flat.raku, $a.both.raku;
    my $b = B.new(x => 2);  o $b.r1.raku, $b.r2.raku, $b.bx, $b.bx2;
    my $c = C.new(x => 3);  o $c.r1, $c.r2, $c.r2b, $c.yy.raku, $c.yl.raku;
    my $d = D.new(x => 4);  o $d.r1.raku, $d.r2.raku, $d.x;
    o E.new.rread, F.new.rread, F.new(rr => 5).rread;
    o (try { $a.assign; 'assigned' }) // "read-only: {$!.^name}";
    # one body, four classes in turn
    o (A.new(x => 'a'), B.new(x => 'b'), C.new(x => 'c'), D.new(x => 'd'), A.new(x => 'e')).map(*.r2).join(' ');
}
# the attribute changes under the read; so does the class
$a.rw = 10;  o $a.r7;
o (try $a.late-read) // "not yet: {$!.^name}";
A.^add_method('late', method { 'added' });
o $a.late-read, $a.r2, $a.r1;
# no object behind `self`
class G { has $.v = 1;  method m { $.v };  method t { self.WHAT.raku } }
o G.new.m, G.t;
o (try G.m) // 'a type object has no attributes';

my @want = q:to/WANT-END/.lines;
117$[1, 2][1, 2]${:a(1)}92method(0, $[1, 2]).Seq(1, 1, 7)
2"overridden"2overridden
333$[1, 2](0, $[1, 2]).Seq
444
6610
read-only: X::Assignment::RO
a overridden c d e
117$[1, 2][1, 2]${:a(1)}92method(0, $[1, 2]).Seq(1, 1, 7)
2"overridden"2overridden
333$[1, 2](0, $[1, 2]).Seq
444
6610
read-only: X::Assignment::RO
a overridden c d e
117$[1, 2][1, 2]${:a(1)}92method(0, $[1, 2]).Seq(1, 1, 7)
2"overridden"2overridden
333$[1, 2](0, $[1, 2]).Seq
444
6610
read-only: X::Assignment::RO
a overridden c d e
10
not yet: X::Method::NotFound
added11
1G
a type object has no attributes
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
