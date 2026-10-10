# Regression: a coercion-typed container coerces EVERY store into an element,
# not only its initialiser, and Hash.append/push merge an existing key as
# Rakudo does.
#
# `my Hash() %h` is Rakudo's Hash[Hash(Any)]. The coercion lived only on the
# variable's name, so the declaration's own `= …` coerced but `%h<k> = …`, a
# slice, `++`, and the push/append/unshift/prepend family ran the plain check
# against the target type and died: "expected Int but got Str". The container
# now carries the coercion type in its element type, as Rakudo's `.of` shows.
#
# Append onto an existing key is `[|old, |new]`, push is `[old, new]`; a Hash
# value slips into its pairs. Append kept a Hash value whole, so
# Text::Table::Simple's `my Hash() %options = %defaults; %options.append: %o`
# built an Array of two Hashes where Rakudo merges them, and its
# t/basics.rakutest died: "Type check failed in assignment to %h; expected
# Hash but got Array".
#
# Every expectation below was checked against Rakudo 2026.09.
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Mu $got, Mu $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub dies-as(&code, $type, $desc) {
    try code();
    @fail.push("$desc: got {$!.^name}, want $type") unless $!.^name eq $type;
}

# the Text::Table::Simple shape
my Hash() %o = a => {x => 1};
%o.append: %(a => {y => 2});
check %o<a>, {x => 1, y => 2}, 'Hash() %h: append of a Hash onto a Hash merges';

# every element store coerces
my Hash() %p;
%p<k> = [1, 2];
check %p<k>, {'1' => 2}, 'Hash() %h: an element assignment coerces';
my Int() %i;
%i<a> = "42";
%i.push: (b => "7");
check %i<a>, 42, 'Int() %h: element assignment';
check %i<b>, 7, 'Int() %h: push of a new key';
my Int() %s;
%s<a b> = "1", "2";
check %s<a>, 1, 'Int() %h: slice assignment';
my Str() %t;
%t.push: (a => 1), (a => 2);
check %t<a>, "1 2", 'Str() %h: push onto an existing key coerces the merged Array';
my Array() %ar = a => 1;
%ar.append: (a => 2);
check %ar<a>, [1, 2], 'Array() %h: append';
my Int() %oh{Str};
%oh<z> = "9";
check %oh<z>, 9, 'Int() %h{Str}: an object hash coerces too';

my Int() @a;
@a.push: "3";
@a.append: "4", "5";
@a.unshift: "1";
@a.prepend: "0";
@a[6] = "9";
check @a[0, 1, 2, 3, 4, 6], (0, 1, 3, 4, 5, 9), 'Int() @a: push/append/unshift/prepend/[i] =';
my Int() @b;
@b[0, 1] = "1", "2";
check @b, Array[Int(Any)].new(1, 2), 'Int() @a: slice assignment';
my Str(Int(Cool)) @n;
@n.push: 4.7;
check @n[0], "4", 'Str(Int(Cool)) @a: a nested source coerces inside out';

# the element type keeps the coercion; the element default is its target
check (my Int() @x).of.raku, 'Int(Any)', '.of of Int() @a';
check (my Rat(Str) %y).of.raku, 'Rat(Str)', '.of of Rat(Str) %h';
my Int() %d;
check %d<missing>, Int, 'a missing element of Int() %h is (Int)';
my Hash() %v;
%v<z><q> = 1;
check %v<z>, {q => 1}, 'Hash() %h: an element autovivifies';

# refusals: the source type, and a failed coercion's own error
dies-as { my Int(Str) %h; %h<a> = 1.5 }, 'X::TypeCheck::Assignment', 'Int(Str) %h refuses a Rat';
dies-as { my Int(Str) @a; @a.push: 1.5 }, 'X::TypeCheck::Assignment', 'Int(Str) @a refuses a pushed Rat';
dies-as { my Int() %h; %h<a> = "abc" }, 'X::Str::Numeric', 'a failed coercion throws its own error';
dies-as { my Int %h = a => 1; %h.append: (a => 2) }, 'X::TypeCheck::Assignment',
    'Int %h: append onto an existing key makes an Array, which Int refuses';

# untyped append/push onto an existing key
my %h1 = a => {x => 1}; %h1.append: (a => 5);
check %h1<a>, [:x(1), 5], 'append slips an existing Hash value into its pairs';
my %h2 = a => 5; %h2.append: (a => {y => 2});
check %h2<a>, [5, :y(2)], '…and an appended Hash value';
my %h3 = a => {x => 1}; %h3.push: (a => 5);
check %h3<a>, [{x => 1}, 5], 'push keeps an existing Hash value whole';
my %h4 = a => (1, 2); %h4.push: (a => 3);
check %h4<a>, [(1, 2), 3], 'push keeps an existing List value whole';
my %h5 = a => 1..2; %h5.append: (a => 3);
check %h5<a>, [1, 2, 3], 'append slips an existing Range value';
my %h6 = :b(2, 3); %h6.append: (:b<Y>);
check %h6<b>, [2, 3, "Y"], 'append slips an existing List value';
my %h7 = a => [1, 2]; %h7.push: (a => [3, 4]); %h7.append: (a => [5, 6]);
check %h7<a>, [1, 2, [3, 4], 5, 6], 'an existing Array takes push whole and append flat';

if @fail { .say for @fail; say "{+@fail} check(s) failed"; exit 1 }
say 'PASS';
