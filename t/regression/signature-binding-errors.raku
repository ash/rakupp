# Regression: what a call that does not fit its signature reports, and a few
# signature shapes that did not parse. Found running mutsu's own t/ suite
# (routines/signature/, 2026-10-04).
#
# - a binding failure carries the Parameter (`.parameter`), and a call made
#   through a variable fails BINDING at run time — only a call that names the
#   routine, with a literal and no default in the way, is the compile-time
#   X::TypeCheck::Argument;
# - an arity failure through a variable or a `|capture` says "Too few/many
#   positionals passed";
# - an anonymous parameter is `<anon>` in the message;
# - a bare `+` and `$? = default` are parameters;
# - an omitted optional's `where` takes part in multi dispatch;
# - `sub f(@a)` refuses a Hash, and a native `int` takes a Bool as its Int.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub err(&code) { CATCH { default { return $_ } }; code(); 'lived' }

# --- binding failures carry the parameter --------------------------------
my &handler = -> Int $n { $n };
my $e = err { handler('nope') };
ck($e.^name, 'X::TypeCheck::Binding::Parameter', 'a call through a variable fails binding');
ck($e.parameter.name, '$n', '... and the exception carries the Parameter');

sub named(Int $n) { $n }
my $s = 'x';
$e = err { named($s) };
ck($e.parameter.name, '$n', 'a variable argument to a named sub fails binding too');

sub defaulted(Int $ = 5) { 2 }
$e = err { defaulted('x') };
ck($e.^name, 'X::TypeCheck::Binding::Parameter', 'a defaulted parameter is left to binding');
ck($e.message.starts-with("Type check failed in binding to parameter '<anon>'"), True,
   'an anonymous parameter is <anon> in the message');

sub positive($x where * > 1) { $x }
$e = err { positive(0) };
ck($e.message, "Constraint type check failed in binding to parameter '\$x'; expected anonymous constraint to be met but got Int (0)",
   'a failed where says what it got');

# --- arity through a variable --------------------------------------------
my &two = sub ($a, $b) { $a + $b };
$e = err { two(1) };
ck($e.message.starts-with('Too few positionals passed'), True, 'too few through a variable');
$e = err { two(|(1, 2, 3)) };
ck($e.message.starts-with('Too many positionals passed'), True, 'too many through a capture');

# --- shapes that parse now -----------------------------------------------
sub alone(+) { 'ok' }
ck(alone(1, 2), 'ok', 'a bare + is a parameter');
ck(alone(), 'ok', '... binding nothing');
sub after($a, +) { $a }
ck(after(1, 2, 3), 1, 'a bare + after a positional');
sub ret(+ --> Str) { 'ret' }
ck(ret(1), 'ret', 'a bare + before -->');
sub opt-default($? = 7) { 'ran' }
ck(opt-default(), 'ran', '$? = 7 is an anonymous optional with a default');
ck((try { EVAL 'my $? = 5'; 'parsed' } // 'refused'), 'refused', 'a Perl 5 $? assignment is still refused');

# --- dispatch runs an omitted optional's where ---------------------------
multi g($x, $y? where { $_ ~~ Int }) { 'constrained' }
multi g($x, $y?) { 'plain' }
ck(g(1), 'plain', 'an omitted optional failing its where loses the candidate');
ck(g(1, 2), 'constrained', 'a supplied value passing it wins');
multi pick(Int $seed = 0) { 32 }
multi pick(Int $seed = 0, $? where { True }) { 64 }
ck(pick(7), 64, 'a passing trailing where-guard makes the narrower candidate');

# --- what binds -----------------------------------------------------------
sub arr(@a) { 'bound' }
ck(err({ arr(%(a => 1)) }).^name, 'X::TypeCheck::Binding::Parameter', 'an @ parameter refuses a Hash');
sub native(int $i) { $i.raku }
ck(native(True), '1', 'a native int parameter takes True as 1');
ck((-> int $i { $i.raku })(False), '0', '... a pointy block too');

# --- more shapes ------------------------------------------------------------
sub some(+bar where *.elems > 1) { bar.elems }
ck(some(1, 2, 3), 3, 'a sigilless slurpy takes a where');
ck(err({ some(5) }).^name, 'X::TypeCheck::Binding::Parameter', '... and checks it');
{
    my \x = 10;
    for 1, 2 -> \x { }
    sub ident($v) { $v }
    ck((ident x), 10, 'a sigilless term named x after a routine name is the term');
}
sub keep(\a, \b) { 'kept' }
my &k = &keep;
my $kv = 1;
ck(($kv >>[&k]<< 0).List, ('kept',), 'a hyper over a callable variable');
ck((try { EVAL 'my Int:U $y is default(0)'; 'bound' } // $!.^name), 'X::Parameter::Default::TypeCheck',
   'an :U variable refuses a defined default');
ck((try { EVAL 'sub f(X::AdHoc $p = "x") { }'; 'bound' } // 'refused'), 'refused',
   'a string default never binds to an exception type');
ck(err({ for ((1, 2),) -> Pair (:key($k), :value($v)) { } }).^name, 'X::TypeCheck::Binding::Parameter',
   'a typed destructure checks the type before unpacking');
sub raw(**@values is raw) { @values }
ck(raw(()).raku, '((),)', '**@ is raw binds a List');
ck(err({ if 5 -> @n { } }).^name, 'X::TypeCheck::Binding::Parameter', 'if 5 -> @n fails binding');
ck((if (1, 2).Seq -> @c { @c.elems }), 2, 'a Seq condition still binds an @ parameter');
ck((try { EVAL 'sub g(:$a, \x) { }'; 'parsed' } // $!.^name), 'X::Parameter::WrongOrder',
   'a sigilless positional after a named is out of order');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
