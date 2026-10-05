# Regression: language behaviour from the fourth batch of Rakudo t/ gap files
# (2026-10-05).
#
# - A role's attribute composed into a class has that class as `.package`.
# - `self.No::Such::foo` is X::Method::InvalidQualifier; `@objs>>.Base::m`
#   dispatches per element through the qualifier.
# - `$<x>=[<sub>]` captures a span: the subrule's captures stay the parent's.
# - `role Numeric { }` shadows the core role for `~~`.
# - A stub cannot replace a real routine declared before it.
# - A Label's gist shows the source around it, its Str `NAME file:line`.
# - `>><>` (a hyper zen angle slice) dies.
# - `$obj.attr = Nil` resets to an `is default(T)` instantiated from the role.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub dies-with($code, $type) { (try { EVAL $code; 'lived' }) // $!.^name eq $type }

role Counting { has $.count = 1 }
class Counter does Counting { }
ck(Counter.^attributes.first(*.name eq '$!count').package.^name, 'Counter', "a role attribute's package");

my class Q { method m { self.No::Such::Type::foo } }
ck((try { Q.new.m; 'lived' }) // $!.^name, 'X::Method::InvalidQualifier', 'an unknown qualifier');
class HA { method m { 'A' } }
class HB is HA { method m { 'B' } }
ck((HB.new, HB.new)>>.HA::m, ('A', 'A'), 'a hyper qualified call');

my grammar Inner {
    token al { $<i>=\w \w* }
    token ur { 'U+' $<x>=[<al>] }
}
my $m = Inner.parse('U+abc', :rule<ur>);
ck(($m<x><i>:exists, ~$m<al><i>), (False, 'a'), 'a bracket alias does not adopt subrule captures');

ck(EVAL('role Numeric { }; so 3.5 ~~ Numeric'), False, 'a user role shadows a core one');
ck(((try { EVAL 'sub foo { 1 }; sub foo {...}'; 'lived' }) // $!.message).contains('Redeclaration of routine'), True,
   'a stub does not replace a real routine');

FOO: my $foo-line = $?LINE;
ck((FOO.gist.contains(': my $foo-line'), FOO.Str.ends-with(".raku:$foo-line")), (True, True), "a Label's gist and Str");

ck((try { my @r = ({:a(1)}, {:a(2)})>><>; 'lived' }) // 'died', 'died', '>><> dies');

my role TypedAttrNil[::T] { has Mu:U $.v is rw is default(T) }
my class TypedAttrNilC does TypedAttrNil[Int] { }
my $obj = TypedAttrNilC.new(v => Str);
$obj.v = Nil;
ck($obj.v === Int, True, 'Nil through an accessor resets to the instantiated default');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
