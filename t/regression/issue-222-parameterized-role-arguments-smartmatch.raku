# Issue #222: `Positional[Int] ~~ Positional[Mu]` was False. Two type objects
# of one parameterized type matched only when their parameters were
# identical, which is right for a class (`Array[Int] !~~ Array[Numeric]`) but
# not for a role: Rakudo asks each argument on the right to ACCEPT the one on
# the left, so `Positional[Int]` is a `Positional[Numeric]`.
#
# PDF::COS::Tie (PDF 0.6.17) checks `$att.type !~~ Positional[Mu]` on
# attributes declared `has PDF::COS::Stream @.Contents`, whose type is
# `Positional[PDF::COS::Stream]`, and warned four times on `use PDF::Content`.
#
# The left argument is a type object, never defined: a `:D` argument on the
# right takes only itself (`Positional[Int:D] !~~ Positional[Numeric:D]`), and
# a `:U` or a bare one asks the nominal type. A nested parameterization asks
# again a level down, a class's still exactly.
#
# Contract: exit 0 + last line PASS. Every answer is Rakudo 2026.09's — run
# this file under `rakudo` too.
my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# the issue's six rows
check 'Positional[Int] ~~ Positional[Mu]',      Positional[Int] ~~ Positional[Mu],      True;
check 'Positional[Int] ~~ Positional[Any]',     Positional[Int] ~~ Positional[Any],     True;
check 'Positional[Int] ~~ Positional[Numeric]', Positional[Int] ~~ Positional[Numeric], True;
check 'Positional[Int] ~~ Positional[Int]',     Positional[Int] ~~ Positional[Int],     True;
check 'Positional[Int] ~~ Positional[Str]',     Positional[Int] ~~ Positional[Str],     False;
check 'Associative[Str] ~~ Associative[Mu]',    Associative[Str] ~~ Associative[Mu],    True;

# …PDF::COS::Tie's check, on the attribute types it reads
class PDF::COS::Stream { }
class Page {
    has PDF::COS::Stream @.Contents;
    has PDF::COS::Stream %.Resources;
    has PDF::COS::Stream $.One;
}
my %att = Page.^attributes.map: { .name => .type };
check 'a typed @-attribute is a Positional[Mu]',   %att<@!Contents> ~~ Positional[Mu],    True;
check 'a typed %-attribute is an Associative[Mu]', %att<%!Resources> ~~ Associative[Mu], True;
check 'a typed $-attribute is neither',            %att<$!One> ~~ Positional[Mu],         False;
check '!~~ agrees',                                Positional[Int] !~~ Positional[Mu],    False;
check '…and so does .ACCEPTS',                     Positional[Mu].ACCEPTS(Positional[Int]), True;

# a class's parameter stays exact; a bare type is no parameterized one
check 'Array[Int] ~~ Array[Numeric]',      Array[Int] ~~ Array[Numeric],      False;
check 'Hash[Int] ~~ Hash[Numeric]',        Hash[Int] ~~ Hash[Numeric],        False;
check 'Array[Int] ~~ Positional[Numeric]', Array[Int] ~~ Positional[Numeric], True;
check 'Array ~~ Positional[Mu]',           Array ~~ Positional[Mu],           False;
check 'Positional ~~ Positional[Mu]',      Positional ~~ Positional[Mu],      False;
check 'Positional[Int] ~~ Positional',     Positional[Int] ~~ Positional,     True;
check 'Iterable ~~ Positional[Mu]',        Iterable ~~ Positional[Mu],        False;

# the other built-in roles, and arity
check 'Callable[Int] ~~ Callable[Numeric]',   Callable[Int] ~~ Callable[Numeric],          True;
check 'Callable[Str] ~~ Callable[Numeric]',   Callable[Str] ~~ Callable[Numeric],          False;
check 'Associative[Int,Str] ~~ [Numeric,Str]', Associative[Int,Str] ~~ Associative[Numeric,Str], True;
check 'Associative[Int,Str] ~~ [Int]',        Associative[Int,Str] ~~ Associative[Int],    False;
check 'Associative[Int] ~~ [Int,Str]',        Associative[Int] ~~ Associative[Int,Str],    False;

# Mu, Any and the classes between
class Base { }
class Derived is Base { }
check 'Positional[Mu] ~~ Positional[Any]',      Positional[Mu] ~~ Positional[Any],         False;
check 'Positional[Any] ~~ Positional[Mu]',      Positional[Any] ~~ Positional[Mu],         True;
check 'Positional[Junction] ~~ Positional[Any]', Positional[Junction] ~~ Positional[Any],  False;
check 'a subclass argument',                    Positional[Derived] ~~ Positional[Base],   True;
check '…and not the other way',                 Positional[Base] ~~ Positional[Derived],   False;
check 'Positional[Bool] ~~ Positional[Int]',    Positional[Bool] ~~ Positional[Int],       True;
check 'Positional[IntStr] ~~ Positional[Str]',  Positional[IntStr] ~~ Positional[Str],     True;

# smileys: the left argument is a type object
check 'Positional[Int:D] ~~ Positional[Int]',       Positional[Int:D] ~~ Positional[Int],       True;
check 'Positional[Int] ~~ Positional[Int:D]',       Positional[Int] ~~ Positional[Int:D],       False;
check 'Positional[Int:D] ~~ Positional[Int:D]',     Positional[Int:D] ~~ Positional[Int:D],     True;
check 'Positional[Int:D] ~~ Positional[Numeric:D]', Positional[Int:D] ~~ Positional[Numeric:D], False;
check 'Positional[Int:D] ~~ Positional[Int:U]',     Positional[Int:D] ~~ Positional[Int:U],     True;
check 'Positional[Int:U] ~~ Positional[Int:D]',     Positional[Int:U] ~~ Positional[Int:D],     False;
check 'Positional[Int:D] ~~ Positional[Mu]',        Positional[Int:D] ~~ Positional[Mu],        True;

# nested parameterizations
check 'P[Array[Int]] ~~ P[Positional[Int]]',  Positional[Array[Int]] ~~ Positional[Positional[Int]],         True;
check 'P[Array[Int]] ~~ P[Array[Numeric]]',   Positional[Array[Int]] ~~ Positional[Array[Numeric]],          False;
check 'P[P[Int]] ~~ P[P[Numeric]]',           Positional[Positional[Int]] ~~ Positional[Positional[Numeric]], True;
check 'P[P[Str]] ~~ P[P[Numeric]]',           Positional[Positional[Str]] ~~ Positional[Positional[Numeric]], False;
check 'P[Array] ~~ P[Positional[Mu]]',        Positional[Array] ~~ Positional[Positional[Mu]],               False;
check 'P[Hash[Int,Str]] ~~ itself',           Positional[Hash[Int,Str]] ~~ Positional[Hash[Int,Str]],        True;
check 'P[Hash[Int,Str]] ~~ P[Associative[Int]]', Positional[Hash[Int,Str]] ~~ Positional[Associative[Int]],  True;

# a user role was already covariant, and stays so
role R[::T] { }
role R3[::A, ::B] { }
check 'R[Int] ~~ R[Numeric]',             R[Int] ~~ R[Numeric],             True;
check 'R[Numeric] ~~ R[Int]',             R[Numeric] ~~ R[Int],             False;
check 'R3[Int,Str] ~~ R3[Numeric,Cool]',  R3[Int,Str] ~~ R3[Numeric,Cool],  True;
check 'R3[Int,Str] ~~ R3[Numeric,Int]',   R3[Int,Str] ~~ R3[Numeric,Int],   False;

.say for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
