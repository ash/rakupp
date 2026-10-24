# A store into an element of a native `int` / `num` / `str` array through
# something that ALIASES the element converts or refuses as one through the
# element's own container does (Rakudo's IntPosRef, NumPosRef, StrPosRef):
#   `my $x := @n[1]`, `my \x = @n[1]`, `@a[0] := @n[0]`
#   `for @n { $_ = … }`, `-> $x is rw`, `<-> $x`, `.kv -> $i, $v is rw`
#   `given @n[0] { $_ = … }`, `with`
#   `sub f($a is rw)`, `sub f(\a)`, `$a is raw`
#   `(@n[0], @n[1]) = …`
# Each took whatever it was given: `$x = 1.5` left 1.5 in an int array, an
# int8 held 300, and `for @n { $_ = Nil }` stored Any. Now True is 1, 300 into
# an int8 is 44, and 1.5, "7", Nil or a big Int is refused as an unbox, the
# way `@n[0] += 1.5` already was. A store answers the element it left:
# `($x = -1)` is 255 for a uint8. `@n[0] ||= Nil` and `&&= Nil` are refused
# too, and so are a native variable's `$i ||= Nil`, `$i .= Str` and `$i = $y`
# with $y holding Any. A Str into a native variable is refused as an unbox,
# as Rakudo words it.
#
# Each answer is Rakudo 2026.09's; this file passes there too.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub refuses(&code, $want, $desc) {
    my $r = try { code(); 'no exception' };
    check $r // $!.message, $want, $desc;
}
my constant RAT  = 'This type cannot unbox to a native integer: P6opaque, Rat';
my constant STR  = 'This type cannot unbox to a native integer: P6opaque, Str';
my constant NIL  = 'Cannot unbox a type object (Nil) to int.';
my constant WIDE = 'Cannot unbox 71 bit wide bigint into native integer. Did you mix int and Int or literals?';

# `my $x := @n[1]`
{
    my int @n = 1, 2;
    my $x := @n[1];
    refuses { $x = 1.5 }, RAT, 'an alias refuses a Rat';
    refuses { $x = "7" }, STR, '…a Str';
    refuses { $x = Nil }, NIL, '…Nil';
    refuses { $x = Int }, 'Cannot unbox a type object (Int) to int.', '…a type object';
    refuses { $x = 2**70 }, WIDE, '…a big Int';
    refuses { $x += 1.5 }, RAT, '…and `+= 1.5`';
    check @n.List, (1, 2), '…storing none of them';
    $x = True;
    check @n.List, (1, 1), 'True is 1';
    $x = <7>;
    check @n.List, (1, 7), 'an IntStr is its Int';
    @n[1] = 5;
    check $x, 5, 'the alias reads the element';
}
{
    my int8 @n = 1, 127;
    my $x := @n[1];
    check ($x = 300), 44, 'an int8 alias answers what it keeps';
    check @n.List, (1, 44), '…wrapped';
    $x = 127;
    $x++;
    check @n.List, (1, -128), '`++` wraps';
    my uint8 @u = 0;
    my $y := @u[0];
    check ($y = -1), 255, 'a uint8 alias answers 255 for -1';
}
{
    my num @n = 1e0;
    my $x := @n[0];
    refuses { $x = 5 }, 'This type cannot unbox to a native number: P6opaque, Int', 'a num alias refuses an Int';
    my num32 @f = 1e0;
    my $f := @f[0];
    $f = 0.1e0;
    check @f[0], 0.10000000149011612e0, 'a num32 alias rounds';
    my str @s = "a";
    my $s := @s[0];
    refuses { $s = 5 }, 'This type cannot unbox to a native string: P6opaque, Int', 'a str alias refuses an Int';
    refuses { $s = Nil }, 'Cannot unbox a type object (Nil) to a str.', '…and Nil';
    check @s.List, ("a",), '…storing neither';
}
{
    my int @n = 1, 2;
    my $x := @n[4];
    check @n.elems, 2, 'binding past the end does not grow the array';
    check $x, 0, '…and reads 0';
    refuses { $x = 1.5 }, RAT, 'a refused store past the end';
    check @n.elems, 2, '…does not grow it either';
    $x = 7;
    check @n.List, (1, 2, 0, 0, 7), 'a store past the end grows it with zeroes';
}
{
    my int @n = 1, 2;
    my \x = @n[0];
    refuses { x = 1.5 }, RAT, 'a sigilless alias refuses a Rat';
    my int8 @m = 1, 2;
    my @a;
    @a[0] := @m[0];
    refuses { @a[0] = 1.5 }, RAT, 'an element bound to one refuses';
    @a[0] = 300;
    check @m.List, (44, 2), '…and wraps';
}

# `for`
{
    my int @n = 1, 2;
    refuses { for @n { $_ = 1.5 } }, RAT, '`$_ = 1.5`';
    refuses { for @n { $_ = 2**70 } }, WIDE, '`$_ = 2**70`';
    refuses { for @n { $_ = Nil } }, NIL, '`$_ = Nil`';
    refuses { for @n { $_ += 1.5 } }, RAT, '`$_ += 1.5`';
    refuses { for @n { $_ .= Str } }, STR, '`$_ .= Str`';
    refuses { my int @z = 0, 0; for @z { $_ ||= Nil } }, NIL, '`$_ ||= Nil`';
    refuses { for @n -> $x is rw { $x = 1.5 } }, RAT, '`-> $x is rw`';
    refuses { for @n <-> $x { $x = 1.5 } }, RAT, '`<-> $x`';
    refuses { for @n.kv -> $i, $v is rw { $v = 1.5 } }, RAT, '`.kv -> $i, $v is rw`';
    refuses { for @n[0, 1] { $_ = 1.5 } }, RAT, 'a slice';
    check @n.List, (1, 2), '…storing none of them';
    for @n { $_ = True }
    check @n.List, (1, 1), 'True is 1';
    my num @f = 1e0, 2e0;
    for @f { $_ += 1 }
    check @f.List, (2e0, 3e0), 'a num `+= 1` is a Num';
    refuses { for @f { $_ = 5 } }, 'This type cannot unbox to a native number: P6opaque, Int', 'a num refuses an Int';
    my str @s = "a", "b";
    for @s { $_ ~= "c" }
    check @s.List, ("ac", "bc"), 'a str `~=`';
    refuses { for @s { $_ = 5 } }, 'This type cannot unbox to a native string: P6opaque, Int', 'a str refuses an Int';
    check @s.List, ("ac", "bc"), '…and keeps what it held';
}
{
    my int8 @n = 1, 2;
    my @seen;
    for @n { $_ = 300; @seen.push($_) }
    check @n.List, (44, 44), 'an int8 wraps';
    check @seen, [44, 44], '…and the topic reads what it keeps';
    my int8 @m = 100, 127;
    for @m { $_ *= 2 }
    check @m.List, (-56, -2), '`*=` wraps';
    for @m <-> $x { $x = 300 }
    check @m.List, (44, 44), '`<-> $x` wraps';
    my uint8 @u = 1;
    my @r;
    for @u { @r.push(($_ = -1)) }
    check @r, [255], 'a uint8 topic answers 255 for -1';
    my int8 @k = 127;
    for @k { $_++ }
    check @k.List, (-128,), '`++` wraps';
    my int8 @l = 1, 2, 3;
    for @l[0, 1] { $_ = 300 }
    check @l.List, (44, 44, 3), 'a slice wraps';
}
{
    my int @n = 1 xx 40;
    for @n { $_ = $_ %% 2 }
    check @n.sum, 0, 'Bools in a longer loop';
    my int8 @m = 1 .. 50;
    for @m { $_ = $_ * 3 }
    check @m.sum, 1777, '…and wraps there';
}

# `given` / `with`
{
    my int @n = 1, 2;
    refuses { given @n[0] { $_ = 1.5 } }, RAT, '`given` refuses';
    refuses { with @n[0] { $_ = 1.5 } }, RAT, '`with` refuses';
    my int8 @m = 1, 2;
    given @m[0] { $_ = 300 }
    check @m.List, (44, 2), '`given` wraps';
}

# `is rw`, `\a` and `is raw` parameters
{
    my int @n = 1, 2;
    sub rw($a is rw) { $a = 1.5 }
    sub raw(\a) { a = 1.5 }
    sub israw($a is raw) { $a = 1.5 }
    refuses { rw(@n[0]) }, RAT, '`is rw`';
    refuses { raw(@n[0]) }, RAT, '`\\a`';
    refuses { israw(@n[0]) }, RAT, '`is raw`';
    check @n.List, (1, 2), '…storing none of them';
    sub yes($a is rw) { $a = True }
    yes(@n[0]);
    check @n.List, (1, 2), 'True is 1';
    my int8 @m = 1, 2;
    sub big(\a) { a = 300 }
    big(@m[0]);
    check @m.List, (44, 2), 'an int8 wraps';
}

# list assignment
{
    my int8 @n = 1, 2;
    refuses { (@n[0], @n[1]) = 300, 1.5 }, RAT, 'a list assignment refuses';
    (@n[0], @n[1]) = 300, True;
    check @n.List, (44, 1), '…and converts';
}

# `||=`, `&&=` and Nil
{
    my int @n = 0;
    refuses { @n[0] ||= Nil }, NIL, '`||= Nil`';
    my int @m = 1;
    refuses { @m[0] &&= Nil }, NIL, '`&&= Nil`';
    @n[0] //= Nil;
    check @n.List, (0,), '`//= Nil` keeps the 0';
    my num @f = 0e0;
    refuses { @f[0] ||= Nil }, 'Cannot unbox a type object (Nil) to a num.', 'a num `||= Nil`';
    my str @s = "";
    refuses { @s[0] ||= Nil }, 'Cannot unbox a type object (Nil) to a str.', 'a str `||= Nil`';
    my $x := @n[0];
    refuses { $x ||= Nil }, NIL, '`||= Nil` through an alias';
    my @a = 0;
    my $y := @a[0];
    $y ||= 5;
    check @a, [5], '`||=` through an alias of a plain array stores';
}

# native variables
{
    my $nil = Nil;
    refuses { my int $i = 1; $i = "7" }, STR, 'a native int refuses a Str as an unbox';
    refuses { my num $n = 0e0; $n = "7" }, 'This type cannot unbox to a native number: P6opaque, Str', 'a native num too';
    refuses { my int $i = 1; $i = $nil }, 'Cannot unbox a type object (Any) to int.', '…and the Any a `$` holds for Nil';
    refuses { my int $i = 0; $i ||= Nil }, NIL, '`$i ||= Nil`';
    refuses { my int $i = 1; $i &&= Nil }, NIL, '`$i &&= Nil`';
    refuses { my int $i = 1; $i .= Str }, STR, '`$i .= Str`';
}

if @fail { .say for @fail; say "FAIL"; exit 1 }
say "PASS";
