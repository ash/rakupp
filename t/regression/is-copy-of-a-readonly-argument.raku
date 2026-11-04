# Regression: an `is copy` parameter is a fresh, writable container, whatever
# was bound to it — a constant included (issue #218).
#
# A constant's value is readonly, and so is a sigilless `\y` parameter's, and
# that mark travels with the value into a call. Every binding arm of an `is
# copy` parameter has to leave it behind, and several did not:
#   - the simple path, which `is copy` joined in 5321f41c, kept what came;
#   - b7c47a57's native wrap put the mark back after the bind cleared it;
#   - a named parameter, a default and a slurpy never cleared it at all.
# So `my constant X = 500; sub f($n is copy) { $n = $n - 1 }; f(X)` died
# "Cannot assign to a readonly variable or a value", in a sub, a method, a
# multi, a pointy block and a `for` loop's.
#
# The bodies end in `~$x`, not a bare `$x`: a body that ends in the variable
# can take a kernel, which never saw the mark, and pass on the broken binder.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub lives(&c, $want, $what) { my $got = try c(); @fail.push("$what: got {$got // $!.message} want $want") unless $got.defined && $got eq $want }
sub dies(&c, $what) { my $ok = False; try { c(); CATCH { default { $ok = True } } }; @fail.push($what) unless $ok }

my constant RO = 5;
my constant BIG = 2**62;
my constant L = (1, 2);

lives({ sub f($x is copy)         { $x = 9; ~$x }; f(RO) },       9, 'untyped');
lives({ sub f(Int $x is copy)     { $x = 9; ~$x }; f(RO) },       9, 'boxed');
lives({ sub f(Str $x is copy)     { $x ~= 'b'; ~$x }; f(~RO) },  '5b', 'a Str');
lives({ sub f(int32 $x is copy)   { $x = 9; ~$x }; f(RO) },       9, 'native');
lives({ sub f(int $x is copy)     { $x = $x * 2; ~$x }; f(BIG) }, -9223372036854775808, 'native, wrapping');
lives({ sub f(:$x is copy)        { $x = 9; ~$x }; f(:x(RO)) },   9, 'named');
lives({ sub f(Int :$x is copy)    { $x = 9; ~$x }; f(:x(RO)) },   9, 'named, typed');
lives({ sub f($x is copy = RO)    { $x = 9; ~$x }; f() },         9, 'a default');
lives({ sub f(int32 $x is copy = RO) { $x = 9; ~$x }; f() },      9, 'a native default');
lives({ sub f(:$x is copy = RO)   { $x = 9; ~$x }; f() },         9, 'a named default');
lives({ sub f($x is copy)         { $x++; ~$x }; f(RO) },         6, 'postfix ++');
lives({ sub f($x is copy)         { $x ~~ s/5/7/; ~$x }; f(RO) }, 7, 's///');
lives({ sub f($x is copy)         { $x = 9; ~$x }; f(L) },        9, 'a constant List');
lives({ sub f($x is copy, $y is copy) { $x = 9; $y = 8; "$x$y" }; f(RO, RO) }, 98, 'two of them');
lives({ sub f($x is copy) { $x = 9; ~$x }; sub g(\y) { f(y) }; g(RO) }, 9, 'from a sigilless parameter');
lives({ class C { method m($x is copy) { $x = 9; ~$x } }; C.m(RO) },  9, 'a method');
lives({ multi f($x is copy) { $x = 9; ~$x }; multi f(Str $s) { }; f(RO) }, 9, 'a multi');
lives({ -> $x is copy { $x = 9; ~$x }(RO) },                    9, 'a pointy block');
lives({ my $r; for (RO,) -> $x is copy { $x = 9; $r = ~$x }; $r }, 9, 'a for loop');
lives({ sub f(*@a is copy)  { @a[0] = 9; ~@a }; f(RO, RO) },  '9 5', 'a flattening slurpy');
lives({ sub f(+@a is copy)  { @a[0] = 9; ~@a }; f(RO, RO) },  '9 5', 'a single-argument slurpy');
lives({ sub f(**@a is copy) { @a[1]++; ~@a }; f(RO, RO) },    '5 6', 'a non-flattening slurpy');
lives({ sub f(*%h is copy)  { %h<a> = 9; ~%h<a> }; f(:a(RO)) },  9, 'a slurpy hash');

# …and the plain parameter, the one `is copy` is there to differ from, stays readonly
dies({ sub f($x) { $x = 9 }; f(RO) },     'a constant to a plain parameter');
dies({ sub f(Int $x) { $x = 9 }; f(RO) }, 'a constant to a typed parameter');
dies({ sub f(:$x) { $x = 9 }; f(:x(RO)) }, 'a constant to a named parameter');
dies({ sub f(*@a) { @a[0] = 9 }; f(RO) }, 'a constant to a slurpy');
lives({ sub f($x is copy) { $x = 9; ~$x }; f(RO); RO }, 5, 'the constant itself is untouched');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
