# --exe: a program asking about its own scope, packages or metaobjects bundles
# with the interpreter instead of answering from the native code, which keeps
# its locals as C++ variables and its routines as C++ functions the engine
# never sees. Compiled, each line below answered differently — mostly empty:
# `MY::<$x>:exists` and `UNIT::{'&f'}:exists` False, `MY::.keys` (), a stash
# term `A::` no entries, `EVAL 'f()'` "Undeclared routine", `EVAL '$x'`
# "not declared", `$x.VAR.dynamic` False, a parameter's `#=` doc Nil,
# `.^methods` nameless Subs, `.^ver` (Any), and `class F is Str` neither a Str
# nor Cool. Found by t/exe/parity.raku. Every line answers the same under
# Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

sub greet() { 'hi' }
my $lex = 1;
check (MY::{'$lex'}:exists), True, 'MY:: :exists';
check (UNIT::{'&greet'}:exists), True, 'UNIT:: :exists';
check MY::.keys.grep('$lex').elems, 1, 'MY::.keys';

class A { our sub x { }; our $y = 1 }
check A::.keys.sort.List, ('$y', '&x'), "a class's stash";

check EVAL('greet()'), 'hi', "EVAL calls the program's sub";
check EVAL('$lex + 1'), 2, "…and reads its variable";

my $d is dynamic = 3;
check $d.VAR.dynamic, True, '.VAR.dynamic';

sub g(Int $a, #= a doc
     ) { }
check &g.signature.params[0].WHY.Str, 'a doc', "a parameter's .WHY";

class Animal { method speak() { 'g' } }
class Dog:ver<1.2.3> is Animal { method speak() { 'woof' } }
check Dog.^methods(:local).grep(*.name eq 'speak').elems, 1, '.^methods';
check Dog.^ver, v1.2.3, '.^ver';

class F is Str { }
check F ~~ Cool, True, 'a class deriving a built-in';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
