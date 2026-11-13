# Regression (RAKUDO-ISSUES-PLAN R3; rakudo#3976): a submethod, a `multi
# submethod` too, is in `.^submethod_table` and not in `.^method_table`.
#
# Before the fix `class Foo { multi submethod FOO { … } }` had FOO in
# `.^method_table` and nothing in `.^submethod_table`, and a plain
# submethod was in both. `.^methods` and `.^method_names` list submethods,
# in Rakudo as here. (Rakudo's tables also hold its own POPULATE, which is
# left out of the comparison.)
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub keys-of($h) { $h.keys.grep(* ne 'POPULATE').sort.List }

class Foo { multi submethod FOO { 1 } }
check 'a multi submethod is a submethod (rakudo#3976)', keys-of(Foo.^submethod_table), ('FOO',);
check '…and no method', keys-of(Foo.^method_table), ();

class Bar { submethod S { 1 }; multi submethod M(Int) { 2 }; multi submethod M(Str) { 3 }; method m { 4 } }
check 'the submethod table', keys-of(Bar.^submethod_table), ('M', 'S');
check 'the method table', keys-of(Bar.^method_table), ('m',);
check '.^method_names lists them all', Bar.^method_names.grep(* ne 'POPULATE').sort.List, ('M', 'S', 'm');
check 'dispatch is unchanged', (Bar.new.S, Bar.new.M(1), Bar.new.M('x'), Bar.new.m), (1, 2, 3, 4);

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
