# --exe: a multi no candidate takes throws the interpreter's X::Multi::NoMatch.
# The native dispatcher threw a bare one — "No matching multi candidate for
# m", no `.dispatcher`, no `.capture` — where the interpreter says which call
# found no candidate and lists the candidates' signatures, as Rakudo does. It
# now hands its candidates, each carrying its signature, to the interpreter's
# own throw (Interpreter::rtMultiNoMatch). Found by t/exe/parity.raku
# (multi-nomatch-dispatcher-capture.raku). Every line answers the same under
# Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

multi m(Int $x) { 'int' }
multi m(Str $s, Int $n) { 'two' }
my $arg = 's';
try m($arg, :k(2));
check $!.^name, 'X::Multi::NoMatch', 'the type';
check $!.message, "Cannot resolve caller m(Str:D, :k(Int)); none of these signatures matches:\n    (Int \$x)\n    (Str \$s, Int \$n)", 'the message';
check $!.dispatcher.name, 'm', '.dispatcher';
check $!.capture.raku, '\("s", :k(2))', '.capture';
check m(1), 'int', 'a candidate still answers';

class A { multi method mm(Int $i) { 'i' }; multi method mm(Str $s) { 's' } }
try A.mm(1.5);
check $!.^name, 'X::Multi::NoMatch', 'a method: the type';
check $!.dispatcher.name, 'mm', "…its dispatcher";
check $!.capture.raku, '\(A, 1.5)', '…its capture, the invocant first';
check A.mm('x'), 's', '…and a candidate still answers';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
