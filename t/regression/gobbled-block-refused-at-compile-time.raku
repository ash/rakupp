# An undeclared type-like name before a statement's block is refused when the
# program compiles, as Rakudo refuses it: "Function 'X::Y' needs parens to
# avoid gobbling block (or perhaps it's a class that's not declared or
# available in this scope?)". Only an EVAL asked; a program's `CATCH { when
# X::Y {} }` ran, and its `if Foo { … }` died "Undeclared name" when it got
# there. Every xblock's condition is asked now (`if`, `elsif`, `unless`,
# `while`, `for`, `given`, `with`, `when`), in a program, under `-c` and in an
# EVAL. A name the unit declares anywhere, a core type, term or enum member,
# a pseudo-package and a package that `class A::B` implies all resolve. (A
# program that `use`s a module other than Test is not judged here, since the
# module may export the name; Rakudo knows what it exports and refuses it.)
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub ran(*@args) {
    my $p = run $*EXECUTABLE, |@args, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    $err ~~ / "Function '" (<-[']>+) "' needs parens to avoid gobbling block" / ?? "gobbled $0" !! $out.trim
}

my $why = "Function 'X::Y' needs parens to avoid gobbling block (or perhaps it's a class that's not declared or available in this scope?)";
ck ran('-e', 'say "before"; CATCH { when X::Y {} }'), 'gobbled X::Y', 'a CATCH';
ck ran('-c', '-e', 'CATCH { when X::Y {} }'), 'gobbled X::Y', '-c';
try EVAL 'CATCH { when X::Y {} }';
ck $!.message.lines.head, $why, 'its message';
for 'if Foo { }', 'if 0 { } elsif Foo { }', 'unless Foo { }', 'while Foo { }', 'for Foo { }',
    'given Foo { }', 'with Foo { }', 'given 1 { when Foo { } }', 'if Foo::Bar { }' -> $code {
    ck ran('-e', "say 'ran'; $code"), "gobbled {$code ~~ /Foo [ '::Bar' ]?/}", $code;
}

for 'class X::Y is Exception {}; CATCH { when X::Y {} }',
    'class A::B {}; if A {} else { say "ok" }; if A::B {} else { say "ok" }',
    'my class L {}; if L {} else { say "ok" }',
    'subset Pos of Int; role R {}; grammar G {}; if Pos {} elsif R {} elsif G {} else { say "ok" }',
    'enum Color <Red Green>; if Green { if Color::Green { say "ok" } }',
    'my constant K = 1; my \T = 2; if K { if T { say "ok" } }',
    'class Q { method m { if Q {} else { say "ok" } } }; Q.m',
    'for Inf { last }; for Empty { }; if NaN { if Less { if Order::More { if SIGINT { say "ok" } } } }',
    'if PROCESS { if X::AdHoc {} elsif Int {} elsif IO::Path {} elsif Hyper {} else { say "ok" } }',
    'given 1 { when Int { say "ok" } }' -> $code {
    ck ran('-e', $code ~ '; say "ok"').lines.tail, 'ok', $code;
}

say 'PASS';
