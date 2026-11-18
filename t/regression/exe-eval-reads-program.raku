# --exe: an EVAL whose code asks for something a native program keeps out of
# the interpreter's sight still keeps the program interpreted (bundled), so
# it answers as interpreted: the program's variables, subs, constants, enum
# values and operators, `$!`, OUTER::, a package-qualified name, a string
# built at run time, and `.raku` of an enum value. Before the EVAL refusal
# was narrowed (exe-eval-literal-native.raku) every EVAL bundled; these must
# go on doing so. Checked both ways by t/exe/parity.raku.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my $y = 7;
check EVAL('$y'), 7, "the program's variable";
my @arr = 1, 2;
check EVAL('@arr').List, (1, 2), "the program's array";
sub f { 42 }
check EVAL('f()'), 42, "the program's sub";
check EVAL('&f.name'), 'f', "the program's sub as a value";
constant K = 3;
check EVAL('K'), 3, "the program's constant";
enum E <ea eb>;
check EVAL('eb'), eb, "the program's enum value";
check eb.raku.EVAL, eb, '.raku of an enum value';
sub infix:<pp>($a, $b) { $a + $b }
check EVAL('1 pp 2'), 3, "the program's operator";
check EVAL('OUTER::<$y>'), 7, 'OUTER::';
our $ov = 9;
check EVAL('$GLOBAL::ov'), 9, 'a package-qualified variable';
my $code = '$y + 1';
check EVAL($code), 8, 'a string built at run time';
try die 'x';
check EVAL('$!.message'), 'x', '$!';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
