# X::Method::NotFound and X::Numeric::DivideByZero made with `.new` have
# Rakudo's `.message` (R8's leftover of rakudo#2434): it was Any.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check X::Method::NotFound.new(:method<x>, :typename<Y>).message,
      "No such method 'x' for invocant of type 'Y'", 'X::Method::NotFound';
check X::Method::NotFound.new(:method<x>, :typename<Y>, :private).message,
      "No such private method '!x' for invocant of type 'Y'", '…a private one';
check X::Numeric::DivideByZero.new(numerator => 5, using => '/').message,
      'Attempt to divide 5 by zero using /', 'X::Numeric::DivideByZero';
check X::Numeric::DivideByZero.new.message, 'Attempt to divide by zero', '…with nothing given';
check X::Numeric::DivideByZero.new(:details<here>).message, 'Attempt to divide by zero here', '…with details';
try X::Method::NotFound.new(:method<z>, :typename<W>).throw;
check $!.message, "No such method 'z' for invocant of type 'W'", 'thrown';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
