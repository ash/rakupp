# Regression: a declaration whose TYPE names nothing.
#
# `my Foo $x` compiled and quietly declared an untyped variable, so a typo'd
# type name was invisible. Rakudo refuses it — "Type 'Foo' is not declared" —
# and S02-types/int-uint.t leans on the refusal: it discovers which native types
# this engine has by `try EVAL "my $_ \$var = 1"` over a candidate list, and with
# nothing refused it kept int1/int2/int4, then died looking `::('int1')` up. The
# file scored 0 of its 104 assertions on that one line.
#
# The rule that governs the check, as for DeclCheck's variable pass: a refusal
# must be a CERTAINTY. Everything below that is NOT refused is refused-by-nobody
# on purpose — a shape the check cannot model, or a unit whose type names it
# cannot enumerate.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub decl($code) { (try EVAL $code) // $!.^name }
use MONKEY-SEE-NO-EVAL;

# --- refused: a name nothing declares ---------------------------------------
check decl('my Foo $x'),       'X::Undeclared', 'an unknown type is refused';
check decl('my int1 $v = 1'),  'X::Undeclared', 'and an unknown NATIVE name';
check decl('my int2 $v = 1'),  'X::Undeclared', 'int2';
check decl('my int4 $v = 1'),  'X::Undeclared', 'int4';
check decl('my Flaot $x'),     'X::Undeclared', 'a typo is what this catches';

# --- not refused: every shape the check does not model ----------------------
check decl('my Int $x = 1; $x'),          1, 'a built-in type';
check decl('my int8 $v = 1; $v'),         1, 'a real native one';
check decl('my $y = 2; $y'),              2, 'no type at all';
check decl('class Bar {}; my Bar $b; 1'), 1, 'a class this unit declares';
check decl('my Bar $b; class Bar {}; 1'), 1, 'even declared BELOW the use';
check decl('subset S of Int; my S $s; 1'), 1, 'a subset';
check decl('role R {}; my R $r; 1'),       1, 'a role';
check decl('enum E <a b>; my E $e; 1'),    1, 'an enum';
check decl('my ::T $t; 1'),                1, 'a type CAPTURE is not a reference';
check decl('my Array[Int] $a; 1'),         1, 'a parameterized type';
check decl('my Foo::Bar $x; 1'),           1, 'a qualified name';
check decl('my Int() $x; 1'),              1, 'a coercion type';
# a unit that imports anything stands down entirely: the import brings names
# this unit never spells, and a top-level `my Imported $x` is built before the
# `use` that would introduce the name has even run
check decl('use Test; my Whatever::Unknown $x; 1'), 1, 'an importing unit stands down';

# --- N-27: what a native container refuses, once it exists ------------------
check (try { my int8 $v; $v = "foo"; 'stored' }) // $!.^name,
      'X::TypeCheck::Assignment', 'a Str does not go into a native int';
check (try { my int64 $v; $v = 2**64; 'stored' }) // $!.^name,
      'X::AdHoc', 'nor does an Int too wide to unbox';
# …but a value that merely overflows the WIDTH is truncated, not refused
check (try { my int8 $v; $v = 300; $v }) // 'threw',  44, 'int8 truncates';
check (try { my uint8 $v; $v = -1; $v }) // 'threw', 255, 'and uint8 wraps';
check (try { my int8 $v; $v = 1; $v }) // 'threw',    1, 'an ordinary value stores';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
