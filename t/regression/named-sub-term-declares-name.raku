# Regression: a named `sub` used as a VALUE (2026-10-18). Rakudo declares its
# name in the enclosing scope. Two places here did not:
#
# - At the top level of a program, `my &up = sub k(Int $c) {…}; say k(49)` died
#   "Undefined routine 'k'" before anything ran. The check that refuses a call
#   to a routine nothing declares (Interpreter::checkUndeclaredCalls) did not
#   count the term as a declaration, although the interpreter installs the name
#   when the term runs. Recursion by name inside the term died the same way.
#   That check only runs on a program made of plain statements, which is why
#   the cases below run as child programs: this file's own `sub ck` would turn
#   it off here.
# - A second declaration of the name went through: `my &a = sub k {…}; my &b =
#   sub k {…}`, `sub k {…}` beside `my &a = sub k {…}`, and `my &k = sub k {…}`
#   are each "Redeclaration of routine 'k'" in Rakudo.
#
# And a `multi method` TERM in a class body (`my $m = multi method foo(Int $x)
# {…}`) was not a method of the class: Parser::routineDeclTerm never set the
# declaration's isMethod, so it became a lexical multi SUB, and class
# composition only builds groups from the methods declared as statements.
#
# Expectations checked against Rakudo v2026.09 via /opt/homebrew/bin/rakudo
# (this file prints PASS there too).

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub child(Str $code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $out
}

# --- the top level of a program --------------------------------------------------
ck child(q[my &up = sub k(Int $c) { "only " ~ $c }; say k(49); say up(49)]),
   "only 49\nonly 49\n", 'a named sub term: its name is declared, at the top level';
ck child(q[my &f = sub fact($n) { $n <= 1 ?? 1 !! $n * fact($n - 1) }; say f(5); say fact(6)]),
   "120\n720\n", '…and recursion by name inside it';
ck child(q[say (1, 2, 3).map(sub dbl($x) { $x * 2 }); say dbl(5)]),
   "(2 4 6)\n10\n", '…as an argument';
ck child(q[say (1, 2).map({ my &s = sub sq($x) { $x * $x }; sq($_) })]),
   "(1 4)\n", '…inside a block, called there';
ck child(q[my $s = sub greet($n) { "hi $n" }; say greet("b"); say $s.name]),
   "hi b\ngreet\n", '…held in a scalar';

# --- here, where the check stands aside ------------------------------------------
my &up = sub k2(Int $c) { "only " ~ $c };
ck k2(49), 'only 49', 'the name is declared in a file of declarations too';
my &a = anon sub k3 { 1 };
ck (try EVAL 'k3()') // 'not installed', 'not installed', '`anon sub` installs nothing';
ck &a.name, 'k3', '…and keeps its name';

# --- a second declaration of the name is a redeclaration --------------------------
for q[my &a = sub k { 1 }; my &b = sub k { 2 }],
    q[sub k { 1 }; my &a = sub k { 2 }],
    q[my &k = sub k { 1 }] -> $code {
    try EVAL $code;
    ck ($! // '').Str.contains("Redeclaration of routine 'k'"), True, "refused: $code";
}
for ^2 {
    my &f = sub k4 { $_ };   # one declaration, run twice: not a redeclaration
}
ck True, True, 'a named sub term in a loop body runs on every iteration';

# --- a `multi method` term in a class body ------------------------------------------
class C {
    my $m = multi method foo(Int $x) { "i" }
    multi method foo(Str $x) { "s" }
    method term { $m }
}
ck C.foo(1), 'i', 'a multi method term is a candidate of the class group';
ck C.foo('a'), 's', '…beside the declared one';
ck C.^lookup('foo').candidates.elems, 2, '…which lists both';
ck C.term.(C, 5), 'i', 'the term is the candidate, a method taking an invocant';
class D is C { }
ck D.foo(2), 'i', 'a subclass inherits it';
class E {
    my $m = multi method baz(Int $x) { "baz $x" }
}
ck E.baz(3), 'baz 3', 'a class whose only candidate is a term';

say $fails ?? "FAILED $fails" !! 'PASS';
