# Issue #184: an element bound out of a typed array lost the array's type.
# `my Int @a = 1, 2, 3; my $e := @a[1]; $e = 'foo'` stored the Str, and
# `$e.VAR.of` died "No such method 'of' for invocant of type 'Int'". In Rakudo
# the constraint rides in the element's own Scalar container, so every alias of
# it — `:=`, a sigilless name, an `is rw` or sigilless parameter, a `for`
# loop's `$_` or `<->` variable, `given`'s topic, `.map`'s `$_` — refuses what
# the element type refuses, and Nil resets the element to its default. Here a
# type constraint lives with the variable's NAME, which an alias does not
# share: each alias now carries the container's guard (elemAliasGuard).
#
# Every answer below is Rakudo 2026.09's, but for `.map`/`.grep`, whose refusal
# here names "the container" rather than `@a` (only the class is checked).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
sub refusal(&code) {
    my $r = 'no error';
    try { code(); CATCH { default { $r = .^name ~ ': ' ~ .message } } }
    $r
}
sub refused($sym, $want, $got) {
    "X::TypeCheck::Assignment: Type check failed for an element of $sym; expected $want but got $got"
}

# the issue's program
{
    my Int @array = 1, 2, 3;
    my $elem := @array[1];
    check "the issue's .VAR.of", $elem.VAR.of, Int;
    check "…its .VAR is a Scalar", $elem.VAR.^name, 'Scalar';
    check "the issue's assignment", refusal({ $elem = 'foo' }), refused('@array', 'Int', 'Str ("foo")');
    check '…leaves the array', @array.raku, 'Array[Int].new(1, 2, 3)';
    $elem = 42;
    check 'a value the type takes still lands', @array.raku, 'Array[Int].new(1, 42, 3)';
}

# the element's Scalar, through the alias and directly
{
    my Int @a = 1, 2, 3;
    my $e := @a[1];
    check '.VAR.name names the container', $e.VAR.name, '@a';
    check '.VAR.default', $e.VAR.default, Int;
    check '@a[0].VAR.of', @a[0].VAR.of, Int;
    check '@a[0].VAR.default', @a[0].VAR.default, Int;
    check '@a[0].VAR.name', @a[0].VAR.name, '@a';
    my @p = 1, 2;
    check 'an untyped element: .VAR.of', @p[0].VAR.of, Mu;
    check '…and its name', @p[0].VAR.name, '@p';
    my Int @d is default(7) = 1;
    check '`is default`: .VAR.default', @d[0].VAR.default, 7;
    check '…and .VAR.of', @d[0].VAR.of, Int;
    my Int:D @s = 1;
    check 'a :D element: .VAR.of', @s[0].VAR.of, Int:D;
}

# every way of storing through `:=`
{
    my Int @a = 1, 2, 3;
    my $e := @a[1];
    $e = Nil;
    check 'Nil resets the element', @a.raku, 'Array[Int].new(1, Int, 3)';
    $e = 5;
    check '~= refused', refusal({ $e ~= 'x' }), refused('@a', 'Int', 'Str ("5x")');
    $e++;
    check '++', @a.raku, 'Array[Int].new(1, 6, 3)';
    check '.= refused', refusal({ $e .= Str }), refused('@a', 'Int', 'Str ("6")');
    check '…none of them landed', @a.raku, 'Array[Int].new(1, 6, 3)';
    my $far := @a[5];
    check 'an element past the end', refusal({ $far = 'x' }), refused('@a', 'Int', 'Str ("x")');
    my @b is default(7) = 1, 2;
    my $be := @b[0];
    $be = Nil;
    check "an untyped `is default` element's Nil", @b.raku, '[7, 2]';
}

# a sigilless name, and the AT-POS / AT-KEY spelling
{
    my Int @a = 1, 2, 3;
    my \s = @a[2];
    check 'my \s = @a[2]', refusal({ s = 'sig' }), refused('@a', 'Int', 'Str ("sig")');
    check '…and its .VAR.of', s.VAR.of, Int;
    my $at := @a.AT-POS(1);
    check ':= @a.AT-POS(1)', refusal({ $at = 'atpos' }), refused('@a', 'Int', 'Str ("atpos")');
    $at = 7;
    check '…is the element itself', @a.raku, 'Array[Int].new(1, 7, 3)';
    my Int %h = a => 1;
    my $ak := %h.AT-KEY('a');
    check ':= %h.AT-KEY("a")', refusal({ $ak = 'atkey' }), refused('%h', 'Int', 'Str ("atkey")');
    $ak = 8;
    check '…is the entry itself', %h<a>, 8;
}

# what the element type is
{
    my Int:D @d = 1, 2;
    my $de := @d[0];
    check 'a :D element refuses a type object', refusal({ $de = Int }),
          refused('@d', 'Int:D', 'Int (Int) (perhaps Nil was assigned to a :D which had no default?)');
    check '…and Nil', refusal({ $de = Nil }),
          refused('@d', 'Int:D', 'Int (Int) (perhaps Nil was assigned to a :D which had no default?)');
    check '…keeping its value', @d.List, (1, 2);
    my Int() @c = 1, 2;
    my $ce := @c[0];
    $ce = "42";
    check 'a coercion type converts', @c[0], 42;
    subset Even of Int where * %% 2;
    my Even @ev = 2, 4;
    my $ee := @ev[0];
    check 'a subset refuses', refusal({ $ee = 3 }), refused('@ev', 'Even', 'Int (3)');
    $ee = 6;
    check '…and takes', @ev[0], 6;
}

# hash elements, and attributes' elements
{
    my Int %h = a => 1;
    my $he := %h<a>;
    check "a hash element's .VAR.of", $he.VAR.of, Int;
    check '…its .VAR.name', $he.VAR.name, '%h';
    check 'a hash element refuses', refusal({ $he = 'baz' }), refused('%h', 'Int', 'Str ("baz")');
    $he = Nil;
    check '…and Nil resets it', %h<a>, Int;
    my $hn := %h<new>;
    check 'a key not there yet', refusal({ $hn = 'x' }), refused('%h', 'Int', 'Str ("x")');
    check '…stays not there', %h<new>:exists, False;
    $hn = 3;
    check '…until a value the type takes', %h<new>, 3;
    my class C { has Int @.a = 1, 2; has Int %.h = b => 2 }
    my $o = C.new;
    my $oe := $o.a[0];
    check "an attribute array's element", refusal({ $oe = 'attr' }), refused('@!a', 'Int', 'Str ("attr")');
    my $ohe := $o.h<b>;
    check "an attribute hash's element", refusal({ $ohe = 'attrh' }), refused('%!h', 'Int', 'Str ("attrh")');
    check '…both unchanged', ($o.a.raku, $o.h<b>), ('Array[Int].new(1, 2)', 2);
}

# parameters
{
    my Int @a = 1, 2, 3;
    sub rw($x is rw) { $x = 'rw' }
    check 'an `is rw` parameter', refusal({ rw(@a[0]) }), refused('@a', 'Int', 'Str ("rw")');
    sub sl(\x) { x = 'sl' }
    check 'a sigilless parameter', refusal({ sl(@a[0]) }), refused('@a', 'Int', 'Str ("sl")');
    check 'a pointy `is rw`', refusal({ -> $x is rw { $x = 'q' }(@a[0]) }), refused('@a', 'Int', 'Str ("q")');
    my $after = 'not reached';
    sub rw-on($x is rw) { $x = 'on'; $after = 'reached' }
    refusal({ rw-on(@a[0]) });
    check 'the refusal is in the assignment', $after, 'not reached';
    sub caught($x is rw) { $x = 'c'; CATCH { default { } } }
    caught(@a[1]);
    check 'a CATCH in the callee stores nothing', @a.raku, 'Array[Int].new(1, 2, 3)';
    my $seen;
    sub nil-rw($x is rw) { $x = Nil; $seen = $x }
    nil-rw(@a[2]);
    check 'Nil through a parameter resets the element', @a.raku, 'Array[Int].new(1, 2, Int)';
    check '…which the parameter reads', $seen, Int;
    sub inc($x is rw) { $x++ }
    inc(@a[0]);
    check '++ through a parameter', @a[0], 2;
    my Int() @c = 1;
    sub to-str($x is rw) { $x = "77" }
    to-str(@c[0]);
    check 'a coercion through a parameter', @c[0], 77;
}

# loops and topics
{
    my Int @a = 1, 2, 3;
    check 'for @a { $_ = … }', refusal({ for @a { $_ = 'w' } }), refused('@a', 'Int', 'Str ("w")');
    check 'for @a <-> $x', refusal({ for @a <-> $x { $x = 'v' } }), refused('@a', 'Int', 'Str ("v")');
    check 'for @a -> $x is rw', refusal({ for @a -> $x is rw { $x = 'r' } }), refused('@a', 'Int', 'Str ("r")');
    check 'for @a -> Cool $x is rw', refusal({ for @a -> Cool $x is rw { $x = 'c' } }), refused('@a', 'Int', 'Str ("c")');
    check 'hyper for', refusal({ hyper for @a { $_ = 'h' } }),
          refused('@a', 'Int', 'Str ("h")').subst('Assignment', 'Assignment+{X::HyperRace::Died}');
    check 'given @a[0] { … }', refusal({ given @a[0] { $_ = 'g' } }), refused('@a', 'Int', 'Str ("g")');
    check '… given @a[0]', refusal({ $_ = 'm' given @a[0] }), refused('@a', 'Int', 'Str ("m")');
    check '.map', refusal({ @a.map({ $_ = 'm' }).eager }).starts-with('X::TypeCheck::Assignment: '), True;
    check '.grep', refusal({ @a.grep({ $_ = 'g'; True }).eager }).starts-with('X::TypeCheck::Assignment: '), True;
    check 'for @a.reverse', refusal({ for @a.reverse { $_ = 'x' } }), refused('@a', 'Int', 'Str ("x")');
    check 'for @a.grep(…)', refusal({ for @a.grep(* > 1) { $_ = 'x' } }), refused('@a', 'Int', 'Str ("x")');
    check 'for @a[0, 1]', refusal({ for @a[0, 1] { $_ = 'x' } }), refused('@a', 'Int', 'Str ("x")');
    check 'for @a.list', refusal({ for @a.list { $_ = 'x' } }), refused('@a', 'Int', 'Str ("x")');
    check '…none of them landed', @a.raku, 'Array[Int].new(1, 2, 3)';
    my Int %h = a => 1;
    check 'for %h.values', refusal({ for %h.values { $_ = 'x' } }), refused('%h', 'Int', 'Str ("x")');
    check '%h.values.map', refusal({ %h.values.map({ $_ = 'x' }).eager }), refused('%h', 'Int', 'Str ("x")');
    check '…nor in the hash', %h<a>, 1;
    for @a { $_ *= 10 }
    check 'a value the type takes still lands', @a.raku, 'Array[Int].new(10, 20, 30)';
    for @a { $_ = Nil }
    check 'Nil resets each element', @a.raku, 'Array[Int].new(Int, Int, Int)';
    my Int() @c = 1, 2;
    for @c { $_ = "5" }
    check 'a coercion type converts in a loop', @c.List, (5, 5);
}

# an untyped container takes anything, as before
{
    my @p = 1, 2;
    my $pe := @p[0];
    $pe = 'fine';
    for @p { $_ ~= '!' }
    sub rw($x is rw) { $x = 'rw' }
    rw(@p[1]);
    check 'an untyped array', @p.raku, '["fine!", "rw"]';
    my %u = a => 1;
    my $ue := %u<a>;
    $ue = 'any';
    check 'an untyped hash', %u<a>, 'any';
}

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
