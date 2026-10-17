# Regression: a grammar rule that names the program's own `my` variable or sub,
# compiled with --exe. The regex engine resolves `$x` in a rule (as an atom, or
# in a `{ … }` block or `<?{ … }>` assertion) by evaluating it in the
# interpreter's scope, and a compiled program's top-level variables and subs
# are C++ globals and functions that scope does not hold: the binary died
# "Variable '$x' is not declared" / "Undefined routine". Such a program now
# falls back to bundling, where the names are the interpreter's own; what the
# engine answers itself (rule parameters, `$0` and `make`, `:my` locals) stays
# native.
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $word = 'hi';
grammar Atom { token TOP { $word } }
ck so(Atom.parse('hi')), True, 'a `my $x` atom';

my @alts = <b c>;
grammar Alts { token TOP { @alts } }
ck so(Alts.parse('c')), True, 'a `my @list` of alternatives';

my $seen = 0;
grammar Count { token TOP { [ \w { $seen++ } ]+ } }
Count.parse('abc');
ck $seen, 3, 'a code block writing a `my` variable';

sub vowel($c) { $c ~~ /<[aeiou]>/ }
grammar Ask { token TOP { (\w) <?{ vowel(~$0) }> } }
ck so(Ask.parse('a')), True, 'an assertion calling a sub (yes)';
ck so(Ask.parse('b')), False, 'an assertion calling a sub (no)';

grammar Native {
    token TOP { <tok('q')> (\d+) { make +$0 } }
    token tok($t) { $t }
}
ck Native.parse('q42').made, 42, 'parameters and make stay native';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
