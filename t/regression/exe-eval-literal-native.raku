# --exe: an EVAL of a literal whose code reads nothing of the program compiles
# natively. Every EVAL refused once (be3038db), because the evaluated code
# cannot see a native program's variables and routines, and 265 of Roast's
# 1,425 files bundled for it. Now only an EVAL that could ask for one of them
# refuses (exe-eval-reads-program.raku): a literal that reads none, a value's
# `.raku` round trip, and a `:lang` the engine has no compiler for compile
# natively, and see the program's classes and grammars, `$/`, dynamic
# variables and `take` as interpreted. Kept native by t/run.raku.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

check EVAL('1 + 2'), 3, 'a literal';
check EVAL("my \$x = 5; \$x * 2"), 10, 'a double-quoted literal declaring its own variable';
check EVAL('sub h { 3 }; h()'), 3, 'a sub the code declares';
check EVAL(q[my @x = 1..3; @x.sum]), 6, 'a q[] literal';
check EVAL('(1,2,3).map({ $_ * 2 }).List'), (2, 4, 6), "a block's own topic";
check '5'.EVAL, 5, '.EVAL on a literal';

class P { has $.x }
check EVAL('P.new(:x(4)).x'), 4, "the program's class";
grammar G { token TOP { a+ } }
check ~EVAL('G.parse("aa")'), 'aa', "the program's grammar";

my @a = 1, 2, [3, 4];
check @a.raku.EVAL, @a, '.raku.EVAL of an Array';
my %h = a => 1, b => [2];
check EVAL(%h.raku), %h, 'EVAL of a Hash .raku';
check (1/128).raku.EVAL, 1/128, 'a Rat round trip';
check P.new(:x(5)).raku.EVAL.x, 5, "an object of the program's class";

check (try { EVAL('1', :lang<NoSuchLang>) } // $!.^name), 'X::Eval::NoSuchLang', ':lang with no compiler';

'ab' ~~ /(b)/;
check ~EVAL('$0'), 'b', 'the match at the top level';
my $*D = 4;
check EVAL('$*D + 1'), 5, 'a dynamic variable';
check (gather { EVAL 'take 1' }).List, (1,), 'take';
check (try { EVAL '1 +' } // 'failed'), 'failed', 'code that does not parse';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
