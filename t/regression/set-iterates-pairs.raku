# A Set iterates as its Pairs, `elem => True`, as a Bag does: `for`, `.list`,
# `.map`, `.grep`, `.sort`, `.first` and `.join` all see `3 => True`. And
# `.flat(:hammer)` flattens what `.flat` keeps whole, an itemized list or
# Hash and an Array's elements. The JavaScript runtime (src/js-rt) walked a
# Set's elements, so `set(3).map({ .key })` was (Any,), and read `.flat` the
# same with `:hammer` or without; t/js/run.raku runs this program under the
# JavaScript host against the interpreter, which already answered so.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @seen;
for set(3) { @seen.push: $_ }
ck @seen.raku, '[3 => Bool::True]', 'for over a Set';
ck set(3).list.raku, '(3 => Bool::True,)', '.list';
ck set(3).map({ .key }).raku, '(3,).Seq', '.map';
ck set(3).grep({ .key == 3 }).raku, '(3 => Bool::True,).Seq', '.grep';
ck set(3, 1).sort.raku, '(1 => Bool::True, 3 => Bool::True).Seq', '.sort';
ck set(3).first.raku, '3 => Bool::True', '.first';
ck set(3).join(','), "3\tTrue", '.join';
ck set(3).keys.List, (3,), '.keys are the elements';
ck bag(3, 3).map({ .value }).List, (2,), 'a Bag, as before';

my $h = %(b => 2);
ck ($h, 1).flat(:hammer).raku, '(:b(2), 1).Seq', ':hammer flattens an itemized Hash';
ck [[1, 2], [3]].flat(:hammer).raku, '(1, 2, 3).Seq', '…and an Array\'s elements';
ck ((1, $(2, 3)), 4).flat(:hammer).raku, '(1, 2, 3, 4).Seq', '…and an itemized list';
ck [[1, 2], [3]].flat.raku, '($[1, 2], $[3]).Seq', 'without it they stay whole';

say 'PASS';
