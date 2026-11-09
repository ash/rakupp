# Regression: `flatmap` is `map` then `.flat`, as Rakudo defines it, and
# flattens the way `.flat` does. It spread each block result ONE level, so
# a nested list stayed nested, `(1, 2).flatmap({ ($_, ($_, ($_,))) })` was
# `(1, (1, (1,)), 2, (2, (2,)))`, and what `.flat` keeps whole was spread:
# an itemized list (`$($_, $_)`), an Array's element (`@a.flatmap(*.self)`
# over lists) and an Array inside an Array. A Hash was kept where `.flat`
# answers its pairs.
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got $got want $want") unless $got eq $want
}

check (1, 2).flatmap({ ($_, ($_, ($_,))) }).raku, '(1, 1, 1, 2, 2, 2).Seq', 'a nested list flattens all the way';
check (1, 2).flatmap({ [$_, [$_]] }).raku, '(1, $[1], 2, $[2]).Seq', 'an Array inside an Array stays whole';
check (1, 2).flatmap({ $($_, $_) }).raku, '($(1, 1), $(2, 2)).Seq', 'an itemized list stays whole';
check (1, 2).flatmap({ %(a => $_) }).raku, '(:a(1), :a(2)).Seq', 'a Hash spreads into its pairs';
my @a = (1, 2), (3, 4);
check @a.flatmap({ $_ }).raku, '($(1, 2), $(3, 4)).Seq', "an Array's elements stay whole";
check ((1, 2), (3, 4)).flatmap({ $_ }).raku, '(1, 2, 3, 4).Seq', "a List's do not";
check ((1, 2), (3, 4)).flatmap({ $_ }).raku, ((1, 2), (3, 4)).map({ $_ }).flat.raku, 'the same as map then flat';

# what one level already got right stays right
check (1, 2).flatmap({ 1..$_ }).raku, '(1, 1, 2).Seq', 'a Range';
check (1, 2).flatmap({ |($_, $_) }).raku, '(1, 1, 2, 2).Seq', 'a Slip';
check (1, 2).flatmap({ Empty }).raku, '().Seq', 'Empty';
check (1, 2).flatmap({ Nil }).raku, '(Nil, Nil).Seq', 'Nil';
check (1, 2).flatmap({ $_ => 1 }).raku, '(1 => 1, 2 => 1).Seq', 'a Pair';
check <a b>.flatmap({ .comb }).raku, '("a", "b").Seq', 'a Seq';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
