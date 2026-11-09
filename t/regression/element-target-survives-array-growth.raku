# An array element that is written after code that grew the array is still
# the element: `@a[0] += do { @a.push(…); 5 }` took a pointer to @a[0],
# ran the right-hand side — whose pushes moved the array's storage — and
# stored through the pointer into the freed block, so @a[0] kept its old
# value and the process died at exit. The same held for `||=` on a computed
# index, `(temp @a[0]) +=`, a `given`/`with` block that assigns its topic,
# and `temp`/`let` of an element with an assignment, whose restore wrote
# there. Found by the review of src/ (docs/dev/findings/src-review-2026-11-09.md,
# A1, A2 and C1). Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

{
    my @a = 1, 2;
    @a[0] += do { @a.push($_) for ^100000; 5 };
    ck @a[0], 6, '+= with a right-hand side that grows the array';
    ck @a.elems, 100002, '…which keeps what was pushed';
}
{
    my @a = 'x', 2;
    @a[0] ~= do { @a.push($_) for ^100000; 'y' };
    ck @a[0], 'xy', '~=';
}
{
    my @a = 1, 2, 3;
    @a[0] max= do { @a.push($_) for ^100000; 5 };
    ck @a[0], 5, 'max=';
}
{
    my @a = 0, 2;
    @a[1 - 1] ||= do { @a.push($_) for ^100000; 5 };
    ck @a[0], 5, '||= on a computed index';
}
{
    my @a = [1, 2], 3;
    @a[0][1] += do { @a[0].push($_) for ^100000; 5 };
    ck @a[0][1], 7, 'an element of an element';
}
{
    my @a = 1, 2, 3;
    @a[0] += do { @a = (); 5 };
    ck @a.elems, 0, 'an array emptied by the right-hand side stays empty';
}
{
    my @a = 1, 2, 3;
    given @a[0] { @a.push($_) for ^100000; $_ = 42 }
    ck @a[0], 42, 'given, its topic assigned after the array grew';
}
{
    my @a = 1, 2, 3;
    with @a[0] { @a.push($_) for ^100000; $_ = 42 }
    ck @a[0], 42, 'with';
}
{
    my @a = 1, 2, 3;
    { temp @a[0] = 9; ck @a[0], 9, 'temp sets the element'; @a.push($_) for ^100000 }
    ck @a[0], 1, 'temp of an element restores it after the array grew';
}
{
    my @a = 1, 2, 3;
    { let @a[0] = 9; @a.push($_) for ^100000; Nil }
    ck @a[0], 1, 'let of an element';
}
{
    my @a = 1, 2, 3;
    { temp @a[5] = 9; @a.push($_) for ^100000 }
    ck @a[5], Any, 'temp of an element that was not there';
}
{
    my @a = 1, 2, 3;
    (temp @a[0]) += do { @a.push($_) for ^100000; 5 };
    ck @a[0], 6, '(temp @a[0]) +=';
}

say 'PASS';
