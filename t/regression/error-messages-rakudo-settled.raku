# Error wording the closed Rakudo issues settled, and one open one whose
# message lost its numerator. Each died in Raku++ too, with other words.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

# rakudo#1987: a method's answer is a value, and the refusal names it
try q|42.abs += 42|.EVAL;
check $!.^name, 'X::Assignment::RO', '42.abs += 42 is X::Assignment::RO';
check $!.message, 'Cannot modify an immutable Int (42)', '…naming the Int (rakudo#1987)';
try { (-5).abs += 1 }
check $!.message, 'Cannot modify an immutable Int (5)', '…the method\'s answer, not the invocant';
try { "ab".uc = 1 }
check $!.message, 'Cannot modify an immutable Str (AB)', 'a Str answer';
my $n = 7;
try { $n.abs = 1 }
check $!.message, 'Cannot modify an immutable Int (7)', 'a variable invocant';
class C { has $.x = 7 }
try { C.new.x = 3 }
check $!.message, 'Cannot modify an immutable Int (7)', 'a read-only attribute, as before';

# rakudo#2109: MixHash.grab is a Failure
my $f = MixHash.new(<a b c>).grab;
check $f.^name, 'Failure', 'MixHash.grab answers a Failure (rakudo#2109)';
check $f.exception.message, '.grab is not supported on a MixHash', '…with Rakudo\'s words';

# rakudo#2595: a subset return type names the value it refused
subset RT of Any where Int;
sub f(--> RT) { 1.8 }
try f();
check $!.message, 'Type check failed for return value; expected RT but got Rat (1.8)',
      'a subset return check names the value (rakudo#2595)';

# rakudo#3598: a Range divided by zero names the endpoint that met the zero
try { say ((Inf .. 5) / 0).min }
check $!.message, 'Attempt to divide Inf by zero using /', '(Inf .. 5) / 0 names Inf (rakudo#3598)';

# rakudo#1266: a port already in use says so, with the address
my $first = IO::Socket::Async.listen('127.0.0.1', 0);
my $tap = $first.tap(-> $c { });
my $port = await $tap.socket-port;
try react whenever IO::Socket::Async.listen('127.0.0.1', $port) { }
check $!.message, "address already in use (address: 127.0.0.1:$port)", 'a busy port (rakudo#1266)';
$tap.close;

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
