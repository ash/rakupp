# Regression: `my $x; $x === Any` answered False.
#
# `Any` has two spellings inside the interpreter — an untyped slot with nothing
# in it holds VT::Any (the default-constructed Value), while the TERM `Any` is
# VT::Type("Any") — and both identity operators opened by rejecting a tag
# mismatch. So the ordinary definedness idiom below was False, while the typed
# form `my Int $y; $y === Int` was True, because a typed declaration stores the
# type object. Everything else already agreed: .WHAT, .defined, .gist, .raku and
# .WHICH all render the two identically.
#
# Found building the ABI A1 extension gate — an extension's rk_any() is JSON
# null, so `$data<key> === Any` was False for every null Rakupp::JSON returned.
# Fixed in applyArith's `===` arm and valueEqv (isAnyTypeObject).
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- the four forms that were wrong -----------------------------------------
my $x;
check ($x === Any), True, 'my $x; $x === Any';
check ($x eqv Any), True, 'my $x; $x eqv Any';
my %h;
check (%h<nope> === Any), True, 'a missing hash key === Any';
my @a;
check (@a[99] === Any), True, 'a missing array element === Any';

# --- and the forms that were already right, which must stay right ------------
# A typed slot holds its own type object.
my Int $i;
my Str $s;
check ($i === Int), True, 'my Int $i; $i === Int';
check ($s === Str), True, 'my Str $s; $s === Str';
check ($i === Any), False, 'a typed slot is NOT the Any type object';

# Identity still discriminates: widening it to "both are undefined" would have
# made every one of these True.
check ($x === Mu),  False, 'Any is not Mu';
check ($x === Int), False, 'Any is not Int';
check ($x === Nil), False, 'Any is not Nil';
check ($x === 0),   False, 'Any is not 0';
check ($x === ""),  False, 'Any is not the empty string';
check (Any === Mu), False, 'the Any term is not Mu';

# Two undefined slots are the same object, as they always were.
my $y;
check ($x === $y), True, 'two untyped undefined slots are identical';

# =:= is CONTAINER identity and must NOT have been touched: Rakudo answers
# False here too, because a container is not the object it holds.
check ($x =:= Any), False, 'a container is not =:= the type object it holds';
check ($x =:= $y),  False, 'two distinct containers are never =:=';
check (Any =:= Any), True, 'the same type object is =:= itself';

# Ordinary identity, unmoved.
check (1 === 1),        True,  '1 === 1';
check (1 === 1.0),      False, '1 === 1.0 (Int is not Rat)';
check (1 === "1"),      False, '1 === "1"';
check (Int === Int),    True,  'Int === Int';
check (Array[Int] === Array[Int]),  True,  'a parameterised type keeps its parameter';
check (Array[Int] === Array[Str]), False, 'Array[Int] is not Array[Str]';
check (Array[Int] === Array),      False, 'Array[Int] is not bare Array';

# Containers built from the two spellings compare equal, which is where this
# would otherwise resurface: is-deeply on any structure holding a null.
check ([$x] eqv [Any]),           True, 'an array holding each spelling is eqv';
check ({a => $x} eqv {a => Any}), True, 'a hash holding each spelling is eqv';

# .WHICH always agreed — the point of the fix is that identity now agrees with it.
check ($x.WHICH eq Any.WHICH), True, '.WHICH matches, and now === does too';
check (set(Any, $x).elems),    1,    'a Set merges the two spellings';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
