# An error a user `infix:<OP>` raises reaches the caller of `OP=`: the
# overload was tried inside a catch of every Raku error, meant for an
# operator that does not take these operands, so a `die` in its body was
# dropped and the built-in operator ran instead —
# `sub infix:<+>(A $a, $b) { die "boom" }; my $x = A.new; $x += 1` left
# $x a Num and said nothing. Only a declined call falls back now. Found by
# the review of src/ (docs/dev/findings/src-review-2026-11-09.md, A4). Every
# line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .message.lines[0] } }
}

class A { has $.v = 0 }
class B { has $.v = 0 }

{
    sub infix:<+>(A $a, $b) { die "boom" }
    my $x = A.new;
    ck refusal({ $x += 1 }), 'boom', 'a die in the operator, through +=';
    ck $x.^name, 'A', '…and the variable is as it was';
    my @a = A.new, A.new;
    ck refusal({ @a[0] += 1 }), 'boom', 'through an element';
    ck @a[0].^name, 'A', '…which is as it was too';
}
{
    sub infix:<+>(B $a, $b) { B.new(v => $a.v - (0 - $b)) }   # (not `+`: that is this sub)
    my $x = B.new(v => 1);
    $x += 2;
    ck $x.v, 3, 'an operator that answers, as before';
    $x += 4;
    ck $x.v, 7, '…twice';
}
{
    sub infix:<~>(A $a, $b) { X::AdHoc.new(payload => "typed").throw }
    my $x = A.new;
    ck refusal({ $x ~= "s" }), 'typed', 'a thrown exception object';
}

say 'PASS';
