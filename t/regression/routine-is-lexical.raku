# Regression: `&?ROUTINE` in a block another routine runs named THAT routine.
#
# `sub g { apply({ &?ROUTINE.name }) }` answered "apply": the routine read was
# the running one. Rakudo's is the routine the block is WRITTEN in, also once
# that routine has returned and from another thread — and `samewith` calls
# that same routine again. Found on the call frames while it runs, and from
# the record a block that asks for it leaves in the routine's scope as it is
# made (lexicalRoutineVal / recordLexicalRoutine). A named regex's code block
# still sees the regex.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

sub apply(&f) { f() }
sub g { apply({ &?ROUTINE.name }) }
ok g() eq 'g', 'in a block another routine runs';
sub g3 { apply(-> { apply({ &?ROUTINE.name }) }) }
ok g3() eq 'g3', 'two blocks deep';
class C { method m { apply({ &?ROUTINE.name }) } }
ok C.m eq 'm', 'in a method';
sub outer { my sub inner { apply({ &?ROUTINE.name }) }; inner() }
ok outer() eq 'inner', 'the innermost routine';
sub mk { return { &?ROUTINE.name } }
ok apply(mk()) eq 'mk', 'after the routine has returned';
sub st { await start { &?ROUTINE.name } }
ok st() eq 'st', 'from a start block, on another thread';
sub fact($n) { $n <= 1 ?? 1 !! $n * &?ROUTINE($n - 1) }
ok fact(5) == 120, 'recursion through it';
sub rec($n) { $n == 0 ?? 'rec' !! (1..1).map({ &?ROUTINE($n - 1) }).head }
ok rec(3) eq 'rec', 'recursion from a block a built-in runs';
my regex r { a { $*X = &?ROUTINE.name } }
my $*X; 'a' ~~ &r;
ok $*X eq 'r', 'a named regex sees itself';
sub esc($n) { return -> { $n > 0 ?? samewith($n - 1) !! 'done' } }
ok esc(2)() ~~ Callable, 'samewith from a block that outlived its routine calls that routine';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
