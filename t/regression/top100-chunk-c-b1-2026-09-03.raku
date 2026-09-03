# Regression: the "big chunk" engine fixes from the 2026-09-03 top-100 work.
# Every expectation is the Rakudo 2026.08 answer.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

# --- C: a JUNCTION hash key autothreads ----------------------------------------
# `'W'|'W*' => X` stores X under BOTH 'W' and 'W*'; `any(@k) => X` under each of
# @k. PDF::Content::Ops keys its %Transition table this way and rakupp kept only
# the collapsed "any" string as one key.
my %h = ("a"|"b") => 1, "c" => 2;
ok(%h.elems == 3, 'an any-junction key expands to one entry per eigenstate');
ok(%h<a> == 1 && %h<b> == 1 && %h<c> == 2, '…each carrying the pair value');
my %j = any("x","y","z") => 9;
ok(%j.elems == 3 && %j<y> == 9, 'any(@list) as a key expands too');
my %e = ("p" & "q") => 7;   # all-junction keys expand the same way
ok(%e.elems == 2 && %e<p> == 7, 'an all-junction key expands as well');
my enum Ctx <Path Text Page>;
my %m = ("BT" => 1, "W"|"W*" => 2, (Path) => 3);
ok(%m.elems == 4, 'a mix of string, junction and enum-value keys');
ok(%m<W> == 2 && %m<W*> == 2 && %m{Path} == 3, '…all resolve');

# --- B1: `require Mod:ver(...)` inside a block ---------------------------------
# The require import-list skip ate to end-of-statement and swallowed the block's
# `}`; the CSS cluster (CSS::Module, ::CSS3::Selectors, ::Specification) writes
# `lives-ok { require CSS::Grammar:ver(v0.3.3+) }, "…"`.
ok(?(try { EVAL 'sub f { 1 }; f() if True; { require Test:ver(v0.0.1+) }; 99' } // False),
   'require Mod:ver(range) parses inside a block and after a modifier');
ok(?(try { EVAL '{ require Test:auth<foo>:ver<1.2> }; 1' } // False),
   'require with :auth and :ver adverbs parses');

# --- MOP: .^add_parent on a runtime-built type --------------------------------
# Test::Mock and Method::Protected reparent a `Metamodel::ClassHOW.new_type`.
{
    class Base { method greet { "hi" } }
    my $t := Metamodel::ClassHOW.new_type(name => 'Derived');
    $t.^add_parent(Base);
    $t.^compose;
    ok($t.^mro.elems == 4, 'add_parent puts the parent in the MRO (Derived Base Any Mu)');
    ok($t.^mro.map(*.^name).grep('Base').elems == 1, '…the named parent is there');
}
# …and the HOW-forwarding spelling Test::Mock uses
{
    class Shape { method sides { 0 } }
    my $m := Metamodel::ClassHOW.new_type();
    $m.HOW.add_parent($m, Shape.WHAT);
    $m.HOW.compose($m);
    ok($m.^mro.map(*.^name).grep('Shape').elems == 1, 'HOW.add_parent($t, Parent) works too');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
