# What a store into an element of a native `int` / `num` / `str` array leaves
# there, by Rakudo's rules. A list assignment (`my int @a = 1.5`) refused what
# a native cannot hold; an element store took anything as it came:
# `@a[0] = True` held a Bool, `@a[0] = 1.5` a Rat, `@a[0] = 2**70` a big Int,
# an int8 element 300, and `@a.push(1)` put an Int into a num array.
#
# nativeElemStore (InterpreterBinding.cpp) is the one rule, and each store
# names its own:
#   `@a[$i] = v`         converts, or X::TypeCheck naming the element
#   `+=`, `||=`          converts, or refuses it as an unbox
#   `++`, `--`           wraps at the width, as the native step does
#   `@a[0, 1] = …`       coerces through .Int / .Num / .Str
#   push / unshift       X::TypeCheck naming the operation
#   append / prepend /
#   splice               refused as an unbox
# An int array takes an Int that fits (a Bool as 0 or 1, an IntStr or an enum
# as its Int), wrapped to its width; a num array a Num (num32 rounds); a str
# array a Str. A big Int is too wide but in a step. A refused store leaves
# the array as it was, unless it grew it. Each answer is Rakudo 2026.09's;
# t/cnp/cases/native-array.raku holds a kernel to the same rules.
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

# `@a[$i] = v`
my int @a = ^3;
@a[0] = True;
check @a.List, (1, 1, 2), 'True into an int array';
@a[1] = <5>;
@a[2] = Order::More;
check @a.List, (1, 5, 1), 'an IntStr and an enum';
refuses { @a[0] = 1.5 }, 'Type check failed in assignment to int array element #0; expected int but got Rat (1.5)', 'a Rat';
refuses { @a[1] = "9" }, 'Type check failed in assignment to int array element #1; expected int but got Str ("9")', 'a Str';
refuses { @a[0] = Nil }, 'Type check failed in assignment to int array element #0; expected int but got Nil (Nil)', 'Nil';
refuses { @a[0] = Int }, 'Type check failed in assignment to int array element #0; expected int but got Int (Int)', 'a type object';
refuses { @a[0] = 2**70 }, 'Cannot unbox 71 bit wide bigint into native integer. Did you mix int and Int or literals?', 'a big Int';
refuses { @a[0] = 2**63 }, 'Cannot unbox 64 bit wide bigint into native integer. Did you mix int and Int or literals?', 'int64 + 1';
check @a.List, (1, 5, 1), '…none of them stored';
refuses { @a[5] = 1.5 }, 'Type check failed in assignment to int array element #5; expected int but got Rat (1.5)', 'past the end';
check @a.elems, 3, '…and the array did not grow';
my $e = (try { @a[0] = 1.5 }) // $!;
check $e.^name, 'X::TypeCheck', 'X::TypeCheck';
my int8 @b = 0, 0;
@b[0] = 300;
check @b.List, (44, 0), 'an int8 wraps';
refuses { @b[1] = 1.5 }, 'Type check failed in assignment to int array element #1; expected int8 but got Rat (1.5)', 'an int8 names itself';
my uint8 @c = 0;
@c[0] = -1;
check @c.List, (255,), 'a uint8 wraps';
refuses { @c[0] = 1.5 }, 'Type check failed in assignment to uint array element #0; expected uint8 but got Rat (1.5)', 'a uint8';
my uint64 @d = 0;
@d[0] = 2**64 - 1;
check @d.List, (18446744073709551615,), 'a uint64 holds 2**64 - 1';
refuses { @d[0] = 2**64 }, 'Cannot unbox 65 bit wide bigint into native integer. Did you mix int and Int or literals?', '…not 2**64';
my num @n = 0e0, 0e0;
@n[0] = <1.5e0>;
check @n.List, (1.5e0, 0e0), 'a NumStr into a num array';
refuses { @n[1] = 5 }, 'Type check failed in assignment to num array element #1; expected num but got Int (5)', 'an Int into a num array';
refuses { @n[1] = True }, 'Type check failed in assignment to num array element #1; expected num but got Bool (Bool::True)', 'a Bool into a num array';
my num32 @f = 0e0;
@f[0] = 0.1e0;
check @f[0], 0.10000000149011612e0, 'a num32 rounds';
my str @s = "";
@s[0] = <5>;
check @s.List, ("5",), 'an IntStr into a str array';
refuses { @s[0] = 5 }, 'Type check failed in assignment to str array element #0; expected str but got Int (5)', 'an Int into a str array';

# through the element's container
my int @g = 0, 1, 2;
@g[2] += True;
check @g.List, (0, 1, 3), '`+= True`';
refuses { @g[2] += 1.5 }, 'This type cannot unbox to a native integer: P6opaque, Rat', '`+= 1.5`';
refuses { @g[2] /= 2 }, 'This type cannot unbox to a native integer: P6opaque, Rat', '`/=`';
check @g.List, (0, 1, 3), '…stored nothing';
@g[0] ||= True;
check @g[0], 1, '`||= True`';
refuses { my int @h = 0; @h[0] ||= 1.5 }, 'This type cannot unbox to a native integer: P6opaque, Rat', '`||= 1.5`';
refuses { my int @h = 0; @h[0] ||= Int }, 'Cannot unbox a type object (Int) to int.', '`||= Int`';
my int8 @i = 100;
@i[0] *= 2;
check @i.List, (-56,), 'an int8 `*=` wraps';
refuses { my int @h = 9223372036854775807; @h[0] += 1 }, 'Cannot unbox 64 bit wide bigint into native integer. Did you mix int and Int or literals?', '`+=` past int64';

# steps
my int @j = 9223372036854775807, 0;
@j[0]++;
@j[1]--;
check @j.List, (-9223372036854775808, -1), '`++` and `--` wrap';
my int8 @k = 127;
@k[0]++;
check @k.List, (-128,), 'an int8 `++` wraps';
my num @m = 0e0;
@m[0]++;
check @m.List, (1e0,), 'a num `++`';

# slices
my int @l = 0, 1, 2;
@l[0, 1] = 1.5, "5";
check @l.List, (1, 5, 2), 'a slice coerces';
my int8 @o = 0, 0;
@o[0, 1] = 300, True;
check @o.List, (44, 1), '…and wraps';
my num @p = 0e0, 0e0, 0e0;
@p[0, 1] = "2.5", True;
check @p.List, (2.5e0, 1e0, 0e0), 'a num slice';
my str @q = "", "";
@q[0, 1] = 5, 2.5;
check @q.List, ("5", "2.5"), 'a str slice';
refuses { @l[0, 1] = 2**70, 1 }, 'Cannot unbox 71 bit wide bigint into native integer. Did you mix int and Int or literals?', 'a slice with a big Int';

# what a store answers: the value as it was handed over, before the wrap, as
# Rakudo's ASSIGN-POS returns it (S09-typed-arrays/native-int.t: a uint
# array's `(@arr[0] = -1)` is -1), and a slice an array of those of its
# family's full width
my uint8 @x = 0, 0;
my $ix = 1;
check (@x[0] = -1), -1, '`(@u8[0] = -1)` is -1';
check (@x[$ix] = 256), 256, '…and 256 under a computed key';
check @x.List, (255, 0), '…while the elements wrap';
my int8 @y = 0;
check (@y[0] = 300), 300, 'an int8 store answers 300';
check (@y[0] = True), 1, 'True answers 1';
my num32 @z = 0e0;
check (@z[0] = 0.1e0), 0.1e0, 'a num32 store answers the Num before it rounds';
my uint @ux = 0;
check (@ux[$ix - 1] = 2**64 - 1), 18446744073709551615, 'a uint store answers 2**64 - 1';
my $was = (@x[0] = -1);
$was++;
check $was, 0, '…a value, not the element';
check (@y[0, 1] = 300, 1), array[int].new(300, 1), 'a slice answers an array[int] of what it took';
check (@ux[0, 1] = 2**64 - 1, 1), array[uint].new(2**64 - 1, 1), '…an array[uint]';
check (@z[0, 1] = 0.1e0, 1), array[num].new(0.1e0, 1e0), '…an array[num]';
check @y.List, (44, 1), '…and its elements wrap';
refuses { @x[0, 1] = -1, 2 }, 'This type cannot unbox to a native unsigned integer: P6opaque, Failure', 'a uint slice takes -1 through .UInt';
my int @gr = 0;
refuses { @gr[3] = 2**70 }, 'Cannot unbox 71 bit wide bigint into native integer. Did you mix int and Int or literals?', 'a big Int past the end';
check @gr.elems, 1, '…did not grow the array';

# the list methods
my int @r;
@r.push(True, <5>);
@r.unshift(True);
@r.append(True, 2);
check @r.List, (1, 1, 5, 1, 2), 'push, unshift and append convert';
refuses { @r.push(1.5) }, 'Type check failed in push to int array; expected int but got Rat (1.5)', 'push';
refuses { @r.push(Nil) }, 'Type check failed in push to int array; expected int but got Nil (Nil)', 'push Nil';
refuses { @r.unshift(1.5) }, 'Type check failed in unshift to int array; expected int but got Rat (1.5)', 'unshift';
refuses { @r.append(1, 1.5) }, 'This type cannot unbox to a native integer: P6opaque, Rat', 'append';
refuses { @r.prepend(1.5) }, 'This type cannot unbox to a native integer: P6opaque, Rat', 'prepend';
refuses { @r.append(Nil) }, 'Cannot unbox a type object (Nil) to int.', 'append Nil';
refuses { @r.push(2**70) }, 'Cannot unbox 71 bit wide bigint into native integer. Did you mix int and Int or literals?', 'push a big Int';
my num @t;
refuses { @t.push(1) }, 'Type check failed in push to num array; expected num but got Int (1)', 'push an Int into a num array';
refuses { @t.append(1) }, 'This type cannot unbox to a native number: P6opaque, Int', 'append an Int into a num array';
@t.push(<1.5e0>);
check @t.List, (1.5e0,), 'push a NumStr';
my int8 @u;
@u.push(300, True);
check @u.List, (44, 1), 'push into an int8';
my int @v = ^5;
@v.splice(1, 1, True);
check @v.List, (0, 1, 2, 3, 4), 'splice converts';
refuses { @v.splice(1, 1, 7, 1.5) }, 'This type cannot unbox to a native integer: P6opaque, Rat', 'splice refuses';
check @v.List, (0, 1, 2, 3, 4), '…before any lands';

# in a loop, where the packed lanes and a kernel store
my int @w = 0 xx 6;
for ^6 -> $x { @w[$x] = $x %% 2 }
check @w.List, (1, 0, 1, 0, 1, 0), 'Bools stored in a loop';

if @fail { .say for @fail; say "FAIL"; exit 1 }
say "PASS";
