# `dd` answers Nil, so a Failure it showed is not sunk after it, and a
# built-in exception's `.raku` shows Rakudo's attributes, in Rakudo's order
# and without a `message` (a method in Rakudo), as in Rakudo (rakudo#2434):
# `dd $x % 0e0` printed the Failure and then threw it.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $r = dd 42;
check $r.raku, 'Any', 'dd answers Nil';
my num $x = 5e0;
dd $x % 0e0;
check 'reached', 'reached', 'a Failure dd showed is not sunk (rakudo#2434)';

try { 5 div 0 }
check $!.raku, 'X::Numeric::DivideByZero.new(using => "div", details => Any, numerator => 5)', 'a thrown one';
check $!.message, 'Attempt to divide 5 by zero using div', 'its message is still there';
check X::Numeric::DivideByZero.new(numerator => 5, using => '/').raku,
      'X::Numeric::DivideByZero.new(using => "/", details => Any, numerator => 5)', 'one made with .new';
check ($x % 0e0).exception.raku, 'X::Numeric::DivideByZero.new(using => "\\%", details => Any, numerator => 5e0)', 'a Failure\'s exception';
try die 'x';
check $!.raku, 'X::AdHoc.new(payload => "x")', 'X::AdHoc, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
