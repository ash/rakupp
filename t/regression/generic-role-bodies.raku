use v6.e.PREVIEW;
# Regression: a parametric role's body is GENERIC (roast S02-types/generics.t).
#
#   - A statement that uses a role parameter — directly, or through a name
#     another such statement declared — runs once per parameterization, with
#     the parameters bound, and the role's methods see that run's lexicals:
#     `my T $v .= new` in R[Int] is 0 and in R[Str] is "". It ran once, at
#     declaration, with T naming nothing, and died "No such method 'new' for
#     invocant of type 'T'".
#   - A class declared there whose parent is parameterized by a role
#     parameter is that parameterization's own: `class A is Array[T]` in
#     R[Int] is R::G::A[Int], and `G::A` names it from the role's methods.
#   - `T:D`, `T:U`, `T()`, `T:D()` and `G::A:D()` resolve the name the scope
#     binds, not the name as written.
#   - A statement-initial `T:D` is a type, not the label `T:` (a label needs
#     whitespace after its colon), and `foo:bar` is one undeclared name.
#   - `has @.a is A`, with `class A is Array[Int]`, keeps A's element type.
#   - The other ways to parameterize a role run its body too: `also does
#     R[…]`, `is R[…]` (the role's pun as a parent), `R.^parameterize(…)`,
#     and a constrained capture `role R[Numeric ::T]`.
#   - `my $x = BEGIN …` in a role body runs once, at the declaration.
#   - A class nested in a `my class` of the body is that parameterization's
#     too (`Box::In` is R::Box::In[Int]).
#   - `so:bar` is still `so` applied to a pair (not an extended name).
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# statement-initial smiley types, and labels still labels
ck((do { Int:D }).^name, 'Int:D', 'a statement-initial Int:D is a type');
sub su { Str:U, 1 }
ck(su().map(*.^name).List, ('Str:U', 'Int'), 'a statement-initial Str:U, 1 is a list');
my @seen;
L1: for ^3 { last L1 if $_ == 1; @seen.push($_) }
L2:
for ^2 { @seen.push("n$_"); last L2 }
L3: # a comment
for ^2 { @seen.push("c$_"); next L3 }
ck(@seen.List, (0, 'n0', 'c0', 'c1'), 'labels before a space, a newline and a comment');
sub foo(|) { 1 }
ck((try EVAL q/foo:bar;/) // $!.^name, 'X::Undeclared::Symbols', '`foo:bar` is one undeclared name');

# the type captures inside the role's methods
my role R[::T] {
    my package G {
        class A is Array[T] {}
    }
    my T $v .= new;
    has @.a is G::A;
    method t-noms { T:D, T:U, T(), T:D() }
    method a-noms { G::A:D, G::A:U, G::A(), G::A:D() }
    method vv { self.^name ~ ":" ~ $v.raku }
}
my class CInt does R[Int] { }
my class CStr does R[Str] { }
ck(CInt.t-noms.map(*.^name).List, <Int:D Int:U Int(Any) Int:D(Any)>, 'T:D T:U T() T:D() in R[Int]');
ck(CStr.t-noms.map(*.^name).List, <Str:D Str:U Str(Any) Str:D(Any)>, '…and in R[Str]');
ck(CInt.t-noms[0] === Int:D, True, 'T:D is Int:D itself');
ck(CInt.a-noms.map(*.^name).List,
   <R::G::A[Int]:D R::G::A[Int]:U R::G::A[Int](Any) R::G::A[Int]:D(Any)>, 'the generic class in R[Int]');
ck(CStr.a-noms.map(*.^name).List,
   <R::G::A[Str]:D R::G::A[Str]:U R::G::A[Str](Any) R::G::A[Str]:D(Any)>, '…and in R[Str]');
ck(CInt.vv ~ ' ' ~ CStr.vv, 'CInt:0 CStr:""', 'the body lexical is per parameterization');
ck(CInt.new(a => (1, 10, 20, 42)).a.List, (1, 10, 20, 42), 'the attribute takes its element type');
my $o = CInt.new(a => (1, 2));
$o.a = (2, 1);
ck($o.a.List, (2, 1), '…and is assignable');
ck((try CInt.new(a => <A B C>)) // $!.^name, 'X::TypeCheck::Assignment', 'R[Int] refuses Strs');
ck((try CStr.new(a => (13, 666))) // $!.^name, 'X::TypeCheck::Assignment', 'R[Str] refuses Ints');

# a generic class is named for its parent's arguments; one that only uses a
# parameter is not generic
my role R2[::K, ::V] {
    my package G { class H is Hash[V, K] {}; class A is Array[V] {}; class B { has K $.x } }
    my class Direct is Array[K] {}
    method all { (G::H, G::A, G::B, Direct) }
}
my class C2 does R2[Str, Int] { }
ck(C2.all.map(*.^name).List, <R2::G::H[Int,Str] R2::G::A[Int] R2::G::B R2::Direct[Str]>, 'generic class names');
ck(C2.all[1].of.^name, 'Int', 'and their element types');

# the transitive part: a statement using a name a generic statement declared
role P[::T] { my T $x; my $y = $x.^name; sub h { "h:" ~ $y }; method y { $y }; method h { h() } }
class PA does P[Int] {}
class PB does P[Str] {}
ck((PA.y, PB.y, PA.h, PB.h), <Int Str h:Int h:Str>, 'dependent statements and subs are generic too');

# value parameters, puns, mixins, and parameters with defaults
role VP[$x = 5] { my $y = $x * 2; method y { $y } }
class VA does VP[3] {}
class VB does VP {}
ck((VA.y, VB.y, VP[7].y, VP.y), (6, 10, 14, 10), 'a value parameter, its default, a pun, the bare role');
role N[::T] { my $n = T.^name; method n { $n }; has $.d = $n }
ck((N[Int].n, N[Str].new.d, (42 but N[Num]).n), <Int Str Num>, 'a pun, a pun instance, a mixin');
role N1[::T] { my $n = "N1:" ~ T.^name; method n1 { $n } }
role N2[::U] does N1[U] { my $m = "N2:" ~ U.^name; method n2 { $m } }
class NC does N2[Rat] {}
ck((NC.n1, NC.n2), <N1:Rat N2:Rat>, 'a role composing a role over its own parameter');

# a container class keeps its element type outside roles too
class IA is Array[Int] {}
class HasIA { has @.a is IA }
ck(IA.of.^name, 'Int', '`is Array[Int]` answers .of');
ck((try HasIA.new(a => <x y>)) // $!.^name, 'X::TypeCheck::Assignment', '`has @.a is IA` checks elements');

# the other parameterization routes
role PRoute[::T] { my $n = T.^name; method n { "n-" ~ $n } }
class AlsoDoes { also does PRoute[Int] }
ck(AlsoDoes.n, 'n-Int', '`also does R[Int]` runs the body for Int');
class IsPun is PRoute[Str] { }
ck(IsPun.n, 'n-Str', '`is R[Str]` inherits the pun of R[Str]');
ck(PRoute.^parameterize(Rat).n, 'n-Rat', '`.^parameterize` is the pun');
role PC[Numeric ::T] { my $n = T.^name; method n { $n } }
class CC does PC[Int] { }
ck(CC.n, 'Int', 'a constrained capture binds');

# BEGIN in a role body is the declaration's
role PBeg[::T] { my $x = BEGIN "at-begin"; method x { $x } }
class CBeg does PBeg[Int] { }
ck(CBeg.x, 'at-begin', '`my $x = BEGIN …` is evaluated once');

# a generic class nested in a `my class`
role PBox[::T] { my class Box { class In is Array[T] { } }; method in { Box::In } }
class CBox does PBox[Int] { }
ck(CBox.in.^name, 'PBox::Box::In[Int]', 'Box::In is this parameterization\'s');

# extended names
ck((so:bar), True, 'so:bar is so applied to a pair');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
