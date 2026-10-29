# Regression: an enum's type object is ONE undefined item, not the list of its
# pairs (found with issue #216).
#
# rakupp keeps an enum's type object as the tagged list of its pairs, and
# everything that did not ask for the enum saw that list: nested in a list, a
# hash or a pair it rendered as `[AAA => 0 BBB => 1 …]`; interpolated, joined
# or concatenated it stringified as "AAA\t0 BBB\t1 …"; `.elems` was 4, `my @a
# = MyEnum` took four Pairs, `for MyEnum` ran four times, `.map`/`.head`/
# `.list` walked the pairs; it was True, `+MyEnum` was 4, `.pop` took a member
# out of the enum for good; and `MyEnum ~ "x"` parsed as the coercion
# `MyEnum(~"x")`, which answered the first member (a subset's name the same:
# only a class's was a term there). Enumeration's own methods
# (`.keys`, `.pairs`, `.pick`, …) still read the members. Every answer below
# is Rakudo 2026.09's.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
# the value, and how many warnings producing it gave
sub warned(&code) {
    my $warnings = 0;
    my $r = code();
    CONTROL { when CX::Warn { $warnings++; .resume } }
    ($r, $warnings)
}

enum MyEnum <AAA BBB CCC DDD>;
my $t = MyEnum;

# rendering, nested
ck (MyEnum, 1).gist, '((MyEnum) 1)', 'in a List it gists as the type';
ck [MyEnum, 1].gist, '[(MyEnum) 1]', '…in an Array';
ck {x => MyEnum}.gist, '{x => (MyEnum)}', '…in a Hash';
my Hash $r = %(:x(MyEnum));
ck $r.gist, '{x => (MyEnum)}', "…the issue's own Hash";
ck (x => MyEnum).gist, 'x => (MyEnum)', '…in a Pair';
ck (MyEnum xx 2).gist, '((MyEnum) (MyEnum))', '…repeated';
ck (MyEnum, 1).raku, '(MyEnum, 1)', 'its .raku nested is its name';
ck [MyEnum].raku, '[MyEnum]', '…with no trailing comma, being no list';
ck {x => MyEnum}.raku, '{:x(MyEnum)}', '…in a Hash, no $[…]';
ck set(MyEnum).raku, 'Set.new(MyEnum)', '…as a Set element';

# string context: "" with the uninitialized warning
ck warned({ "[{MyEnum}]" }), ('[]', 1), 'interpolated';
ck warned({ "[$t]" }), ('[]', 1), '…from a variable';
ck warned({ "a" ~ MyEnum ~ "b" }), ('ab', 1), '`~`, which is the infix after the type name';
ck warned({ [~] MyEnum, "x" }), ('x', 1), '…reduced';
ck warned({ (MyEnum, 1).join("-") }), ('-1', 1), 'joined';
ck warned({ sprintf("%s|", MyEnum) }), ('|', 2), "sprintf's %s, one argument (Rakudo warns twice)";
subset Small of Int where * < 10;
ck (quietly Small ~ "y"), 'y', "a subset's name before `~` is a term too";

# one item
my @a = MyEnum;
ck (@a.elems, @a[0] === MyEnum), (1, True), '`my @a = MyEnum` is one element';
my @seen; for MyEnum { @seen.push($_) }
ck @seen.elems, 1, '`for MyEnum` runs once';
ck (MyEnum, MyEnum).flat.elems, 2, '.flat does not open it';
ck (MyEnum.elems, $t.elems, elems(MyEnum), MyEnum.end), (1, 1, 1, 0), '.elems, the sub and .end';
ck MyEnum.map(*.^name).List, ('MyEnum',), '.map calls once';
ck (MyEnum.head === MyEnum, MyEnum.first === MyEnum), (True, True), '.head and .first are the type';
ck MyEnum.list.raku, '(MyEnum,)', '.list';
ck MyEnum.Array.raku, '[MyEnum]', '.Array';
ck MyEnum.grep(*.defined).elems, 0, '.grep sees one undefined item';
ck MyEnum.Set.elems, 1, '.Set';

# undefined: False, no number
ck (?MyEnum, so($t), !MyEnum, MyEnum.Bool, MyEnum.so), (False, False, True, False, False), 'it is False';
ck (MyEnum ?? 1 !! 2), 2, '…as a condition';
ck (MyEnum // 'd'), 'd', '…and undefined';
ck warned({ (try +MyEnum) // $!.^name }), ('X::Constructor::BadType', 1), '`+MyEnum` warns and refuses, as .Numeric';
ck (try MyEnum + 1) // $!.^name, 'X::Numeric::Uninitialized', '`MyEnum + 1` refuses';
ck (try MyEnum.Num) // $!.^name, 'X::Parameter::InvalidConcreteness', '.Num wants an instance';

# its members stay what was declared
ck (try MyEnum.push(1)) // $!.^name, 'X::Assignment::RO', '.push is refused';
ck (try MyEnum.pop) // $!.^name, 'X::Method::NotFound', '.pop is no method of it';
ck (try my %h = MyEnum) // $!.^name, 'X::Hash::Store::OddNumber', 'a Hash initializer sees one item';
ck MyEnum.keys.sort.List, <AAA BBB CCC DDD>, '…and Enumeration still reads the members';
ck (MyEnum.pairs.elems, MyEnum.kv.elems, MyEnum.enums.elems), (4, 8, 4), '….pairs, .kv, .enums';
ck MyEnum.pick.^name, 'MyEnum', '…and .pick picks one';
ck MyEnum(2), CCC, 'the call by name still coerces';

say 'PASS';
