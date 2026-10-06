# Issue #131: `"item-$_-x"` — `_` starts an identifier, so `$_-x` is ONE
# variable name, not `$_` followed by the text `-x`.
#
#   The undeclared-variable gate exempted every name whose first character
#   after the sigil was `_` (meant for `$_` alone), so `$_-x`, `$_foo` and
#   `$__` slipped through: an uninitialized-value warning at run time and an
#   empty interpolation, where Rakudo refuses to compile ("Variable '$_-x' is
#   not declared"). Only the bare `$_` (and `@_`, `%_`) is exempt now.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub ev($code) { my $r is default(Nil) = try EVAL $code; $! ?? $!.^name !! $r }

check ev('for 7 { "item-$_-x" }'),        'X::Undeclared', '"item-$_-x" interpolates one name';
check ev(q[for 7 { "a$_'s" }]),           'X::Undeclared', q["a$_'s" interpolates one name];
check ev('$_foo'),                        'X::Undeclared', '$_foo';
check ev('for 8 { $_-x }'),               'X::Undeclared', '$_-x outside a string';
check ev('$__'),                          'X::Undeclared', '$__';

# declared, the same names are ordinary variables
check ev('my $_-x = 4; "w-$_-x"'),        'w-4',  'declared $_-x';
check ev('my $_foo = 2; $_foo'),          2,      'declared $_foo';

# …and `$_` itself is untouched: `-1` does not continue an identifier
check ev('my $r; for 7 { $r = "q-$_-1" }; $r'), 'q-7-1', '"q-$_-1"';
check ev('my $r; for 8 { $r = $_-1 }; $r'),     7,       '$_-1';
check ev('my $r; for 7 { $r = "x$_" }; $r'),    'x7',    '"x$_"';
check ev('sub f { @_.elems }; f 1, 2'),         2,       '@_';
check ev('sub f { %_.elems }; f :a, :b'),       2,       '%_';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
