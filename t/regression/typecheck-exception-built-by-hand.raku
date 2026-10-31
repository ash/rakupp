# Regression: an X::TypeCheck built by hand renders its message from its
# attributes, as one the engine threw does (issue #77, FCO's
# X::TypeCheck::Assignment item).
#
# `X::TypeCheck::Assignment.new(symbol => '$x', got => 42, expected => Str)
# .message` was undefined: the class was registered on first use with no
# `method message`, while Rakudo gives the whole family one. Red builds
# exactly that one when a column refuses a value.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck X::TypeCheck::Assignment.new(symbol => '$x', got => 42, expected => Str).message,
   'Type check failed in assignment to $x; expected Str but got Int (42)', 'assignment';
ck X::TypeCheck::Assignment.new(symbol => '@a', got => 's', expected => Int).message,
   'Type check failed for an element of @a; expected Int but got Str ("s")', 'an element of an array';
ck X::TypeCheck::Assignment.new(symbol => '$x', got => 5, expected => Int).message,
   'Type check failed in assignment to $x; expected Int but got 5', 'a value of the expected type shows as itself';
ck X::TypeCheck::Assignment.new(symbol => '$x', got => 5, expected => Hash).message,
   'Type check failed in assignment to $x; expected Hash but got Int', 'an Associative names the type only';
ck X::TypeCheck::Assignment.new(symbol => '$x', got => Int, expected => Int:D).message,
   'Type check failed in assignment to $x; expected Int:D but got Int (Int) (perhaps Nil was assigned to a :D which had no default?)',
   'a :D the type object misses';
ck X::TypeCheck::Assignment.new(symbol => '$x', got => 'a' x 40, expected => Int).message,
   'Type check failed in assignment to $x; expected Int but got Str ("aaaaaaaaaaaaaaaaaaa...)', 'a long value is cut short';

ck X::TypeCheck::Binding.new(got => 42, expected => Str).message,
   'Type check failed in binding; expected Str but got Int (42)', 'binding';
ck X::TypeCheck::Binding::Parameter.new(got => 42, expected => Str, symbol => '$p').message,
   "Type check failed in binding to parameter '\$p'; expected Str but got Int (42)", 'a parameter';
ck X::TypeCheck::Binding::Parameter.new(got => 42, expected => Str).message,
   'Type check failed in binding to anonymous parameter; expected Str but got Int (42)', 'an anonymous parameter';
ck X::TypeCheck::Return.new(got => 42, expected => Str).message,
   'Type check failed for return value; expected Str but got Int (42)', 'a return value';
ck X::TypeCheck.new(got => 42, expected => Str, operation => 'frobbing').message,
   'Type check failed in frobbing; expected Str but got Int (42)', 'the base class with its operation';
ck X::TypeCheck::Splice.new(got => 42, expected => Str, action => 'splice').message,
   'Type check failed in splice; expected Str but got Int (42)', 'a splice';
ck X::TypeCheck::Argument.new(objname => 'f', arguments => ('Int',), signature => '(Str $x)').message,
   'Calling f(Int) will never work with declared signature (Str $x)', 'an argument list';

my $caught;
try { X::TypeCheck::Assignment.new(symbol => '$x', got => 42, expected => Str).throw; CATCH { default { $caught = $_ } } }
ck $caught.message, 'Type check failed in assignment to $x; expected Str but got Int (42)', 'thrown, the same message';
ck ($caught.got, $caught.expected.^name), (42, 'Str'), '…and the attributes';

# what the engine throws keeps its own
my $thrown;
try { my Str $s = 42; CATCH { default { $thrown = .message } } }
ck $thrown, 'Type check failed in assignment to $s; expected Str but got Int (42)', 'an engine-thrown one';

say 'PASS';
