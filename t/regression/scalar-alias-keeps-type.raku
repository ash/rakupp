# A scalar alias dropped its variable's type: `my Int $a = 1; my $b := $a;
# $b = 'x'` stored the Str, as did `sub f($x is rw) { $x = 'y' }; f($a)`,
# `for $a { $_ = 'z' }` and `given $a { $_ = 'z' }`, and `$b.VAR.of` was
# (Mu). Issue #184's cause, for a `$` variable rather than an element: a type
# here lives with the variable's NAME, in the declaring scope's tables, and an
# alias shares the cell but not the name. In Rakudo the type is the
# container's, and every alias of it refuses as the variable does, naming the
# variable. A typed variable's alias now holds a Proxy over its cell carrying
# the variable's record (scalarAliasOf): its name, type, smiley, coercion and
# default.
#
# Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
sub refusal(&code) {
    my $r = 'no error';
    try { code(); CATCH { default { $r = .^name ~ ': ' ~ .message } } }
    $r
}
sub refused($name, $want, $got) {
    "X::TypeCheck::Assignment: Type check failed in assignment to $name; expected $want but got $got"
}

# `:=`, and every way of storing through it
{
    my Int $a = 1;
    my $b := $a;
    check 'the alias is the container', $b =:= $a, True;
    check '$b = "x"', refusal({ $b = 'x' }), refused('$a', 'Int', 'Str ("x")');
    check '…leaves $a', $a, 1;
    $b = 5;
    check 'a value the type takes lands', $a, 5;
    $b = Nil;
    check 'Nil resets the variable', $a, Int;
    $a = 1;
    check '~=', refusal({ $b ~= 'z' }), refused('$a', 'Int', 'Str ("1z")');
    $b++;
    check '++', $a, 2;
    check '.=', refusal({ $b .= Str }), refused('$a', 'Int', 'Str ("2")');
    my $c := $b;
    check 'an alias of the alias', refusal({ $c = 'c' }), refused('$a', 'Int', 'Str ("c")');
    my \s = $a;
    check 'a sigilless name', refusal({ s = 's' }), refused('$a', 'Int', 'Str ("s")');
    my &f = { $b = 'clo' };
    check 'a closure over the alias', refusal({ f() }), refused('$a', 'Int', 'Str ("clo")');
    check 'a thread', refusal({ await start { $b = 't' } }),
          refused('$a', 'Int', 'Str ("t")').subst('Assignment', 'Assignment+{X::Await::Died}');
    check 'none of them landed', $a, 2;
}

# the alias's Scalar is the variable's
{
    my Int $a = 1;
    my $b := $a;
    check '.VAR.of', $b.VAR.of, Int;
    check '.VAR.default', $b.VAR.default, Int;
    check '.VAR.name', $b.VAR.name, '$a';
    check '.VAR.^name', $b.VAR.^name, 'Scalar';
    my \s = $a;
    check "a sigilless name's .VAR.of", s.VAR.of, Int;
    my Int $d is default(7) = 1;
    my $db := $d;
    check '`is default`: .VAR.default', $db.VAR.default, 7;
    check '….VAR.of', $db.VAR.of, Int;
    check "…and the variable's own .VAR.of", $d.VAR.of, Int;
    $db = Nil;
    check '…and Nil resets to it', $d, 7;
    my Int:D $x = 1;
    check 'a :D variable: .VAR.of', $x.VAR.of, Int:D;
    my Int() $co = 1;
    my $cb := $co;
    check 'a coercion type: .VAR.of', ($co.VAR.of.^name, $cb.VAR.of.^name), ('Int(Any)', 'Int(Any)');
}

# what the type is
{
    my Int:D $d = 1;
    my $dd := $d;
    check 'a :D variable refuses a type object', refusal({ $dd = Int }),
          refused('$d', 'Int:D', 'Int (Int) (perhaps Nil was assigned to a :D which had no default?)');
    check '…and keeps its value', $d, 1;
    my Int() $co = 1;
    my $cb := $co;
    $cb = "42";
    check 'a coercion type converts', $co, 42;
    subset Even of Int where * %% 2;
    my Even $ev = 2;
    my $eb := $ev;
    check 'a subset refuses', refusal({ $eb = 3 }), refused('$ev', 'Even', 'Int (3)');
    $eb = 6;
    check '…and takes', $ev, 6;
    my Str $s = 'x';
    my $sb := $s;
    $sb ~= 'y';
    check 'a Str takes a Str', $s, 'xy';
    check '…not an Int', refusal({ $sb = 5 }), refused('$s', 'Str', 'Int (5)');
}

# parameters
{
    my Int $a = 1;
    sub rw($x is rw) { $x = 'rw' }
    check 'an `is rw` parameter', refusal({ rw($a) }), refused('$a', 'Int', 'Str ("rw")');
    sub sl(\x) { x = 'sl' }
    check 'a sigilless parameter', refusal({ sl($a) }), refused('$a', 'Int', 'Str ("sl")');
    sub raw($x is raw) { $x = 'raw' }
    check 'an `is raw` parameter', refusal({ raw($a) }), refused('$a', 'Int', 'Str ("raw")');
    check 'a pointy `is rw`', refusal({ -> $x is rw { $x = 'p' }($a) }), refused('$a', 'Int', 'Str ("p")');
    sub typed(Int $x is rw) { $x = 'ty' }
    check 'a typed `is rw` parameter', refusal({ typed($a) }), refused('$a', 'Int', 'Str ("ty")');
    sub named(:$x! is rw) { $x = 'nm' }
    check 'a named `is rw` parameter', refusal({ named(:x($a)) }), refused('$a', 'Int', 'Str ("nm")');
    sub chain1($x is rw) { chain2($x) }
    sub chain2($y is rw) { $y = 'ch' }
    check 'through two `is rw` parameters', refusal({ chain1($a) }), refused('$a', 'Int', 'Str ("ch")');
    sub inner($x is rw) { my $z := $x; $z = 'in' }
    check 'an alias of the parameter', refusal({ inner($a) }), refused('$a', 'Int', 'Str ("in")');
    check 'the parameter is the variable: .VAR', -> $x is rw { ($x.VAR.of, $x.VAR.name) }($a), (Int, '$a');
    check 'none of them landed', $a, 1;
    sub ok-store($x is rw) { $x = 42 }
    ok-store($a);
    check 'a value the type takes lands', $a, 42;
    my Int $d is default(3) = 9;
    sub nil-it($x is rw) { $x = Nil }
    nil-it($d);
    check 'Nil through a parameter resets the variable', $d, 3;
}

# loops and topics
{
    my Int $a = 1;
    my Int $c = 2;
    check 'for $a', refusal({ for $a { $_ = 'f' } }), refused('$a', 'Int', 'Str ("f")');
    check 'for $a <-> $v', refusal({ for $a <-> $v { $v = 'v' } }), refused('$a', 'Int', 'Str ("v")');
    check 'for $a -> $v is rw', refusal({ for $a -> $v is rw { $v = 'r' } }), refused('$a', 'Int', 'Str ("r")');
    check 'for $a, $c', refusal({ for $a, $c { $_ = 'fl' } }), refused('$a', 'Int', 'Str ("fl")');
    check 'for $a, $c -> $x is rw', refusal({ for $a, $c -> $x is rw { $x = 'fr' } }), refused('$a', 'Int', 'Str ("fr")');
    check 'given $a { … }', refusal({ given $a { $_ = 'g' } }), refused('$a', 'Int', 'Str ("g")');
    check '… given $a', refusal({ $_ = 'gm' given $a }), refused('$a', 'Int', 'Str ("gm")');
    check 'with $a { … }', refusal({ with $a { $_ = 'w' } }), refused('$a', 'Int', 'Str ("w")');
    check 'none of them landed', ($a, $c), (1, 2);
    for $a, $c { $_ *= 10 }
    check 'values the type takes land', ($a, $c), (10, 20);
    my $b := $a;
    for $b { $_++ }
    check 'for over the alias', $a, 11;
    check '…refuses as the variable', refusal({ for $b { $_ = 'b' } }), refused('$a', 'Int', 'Str ("b")');
    my Int %h = a => 1, b => 2;
    check 'for over a typed hash slice', refusal({ for %h<a b> { $_ = 'x' } }),
          "X::TypeCheck::Assignment: Type check failed for an element of %h; expected Int but got Str (\"x\")";
}

# an untyped variable takes anything, as before
{
    my $u = 1;
    my $ub := $u;
    $ub = 'fine';
    sub ustore($x is rw) { $x = 'rw' }
    ustore($u);
    check 'an untyped variable through its aliases', $u, 'rw';
    for $u { $_ ~= '!' }
    check '…and a loop over it', $u, 'rw!';
    check "…whose alias's .VAR.of is Mu", $ub.VAR.of, Mu;
    my $x = 1;
    for $x, $x { $_++ }
    check 'one variable twice', $x, 3;
}

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
