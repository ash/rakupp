# `,=` — the assignment metaoperator over `infix:<,>`. GitHub issue #85.
#
# `%h ,= 5 => 4` grew the hash on Rakudo and REPLACED it here: the lexer had no
# `,=` token, so the two characters came out as a `,` and an `=`, `%h ,` parsed
# as a one-element list with a trailing comma, and `(%h) = 5 => 4` is a real (if
# useless) list assignment — which is why nothing threw. The reporter's second
# message is the part that matters: "rakupp does not throw: it just does
# something not intended."
#
# The rule, and the only rule: **`A ,= B` is always `A = A, B`** — the comma
# applied to the two operands and the result assigned, with whatever that
# assignment already means for A's container. Nothing about `,=` is special, so
# every row below is checked against the written-out spelling as well as against
# its expected value.
#
# Passes under both rakupp and Rakudo: what it asserts is what the two share.
# The one place they differ is not asserted — `@a ,= 3` and `$x ,= 2` build a
# list whose first element is the container being written, so its RENDERING is
# a cycle both engines print in their own notation. The shape of it (how many
# elements, and which) is identical, and that is what is checked.

my $fails = 0;
sub check($got, $want, Str $desc) {
    if $got eqv $want || $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku}] WANT [{$want.raku}]";
    }
}

# ---- the report ------------------------------------------------------------
{
    my %h = 1 => 2, 2 => 3, 4 => 3;
    %h ,= 5 => 4;
    check %h.elems, 4, 'a pair appended to a hash keeps the keys already there';
    check %h{'5'}, 4, '…and adds its own';
    check %h{'1'}, 2, '…and the first key survives';
}

# the same without the spaces around the operator: one token either way
{
    my %h = 1 => 2;
    %h,=5 => 4;
    check %h.elems, 2, '`%h,=5 => 4` lexes as the metaop, not as a comma and an =';
}

{
    my %h;
    %h ,= 1 => 2;
    check %h.elems, 1, 'an empty hash takes the first pair';
}

# a Slip on the right splices, as it does in any comma list
{
    my %h = 1 => 2;
    my @p = 5 => 4, 6 => 7;
    %h ,= |@p;
    check %h.elems, 3, 'a slipped list of pairs appends all of them';
}

# ---- and it is A = A, B, for every container --------------------------------
# An `@`/`$` target builds a list whose first element is the container being
# assigned. Both engines answer that same shape; only the gist of the cycle
# differs, so the shape is what is asserted.
{
    my @a = 1, 2;
    @a ,= 3;
    check @a.elems, 2, 'an array becomes (itself, the new value) — two elements';
    check @a[1], 3, '…with the new value second';
    check @a[0].elems, 2, '…and the old contents as the first, one element';
}

{
    my $x = 1;
    $x ,= 2;
    check $x.elems, 2, 'a scalar holds the two-element list';
    check $x[1], 2, '…with the new value second';
}

{
    my %h = a => 1;
    %h<b> ,= 9;
    check %h<b>.elems, 2, 'a hash ELEMENT is a scalar: it takes the list, not a pair';
    check %h<b>[1], 9, '…new value second';
    check %h<a>, 1, '…and the sibling key is untouched';
}

{
    my @a = 1, 2;
    @a[0] ,= 9;
    check @a[0].elems, 2, 'an array element takes the list too';
    check @a[1], 2, '…and its sibling is untouched';
}

# the target is read ONCE — a subscript's index must not run twice
{
    my $calls = 0;
    sub k() { $calls++; 'b' }
    my %h = a => 1;
    %h{k()} ,= 9;
    check $calls, 1, 'the target is evaluated once, so a subscript cannot run twice';
}

# ---- the written-out spelling answers the same ------------------------------
{
    my %m = 1 => 2, 2 => 3;   my %w = 1 => 2, 2 => 3;
    %m ,= 5 => 4;             %w = (%w, 5 => 4);
    check %m.raku, %w.raku, 'hash: `%h ,= p` is `%h = (%h, p)`';
}
{
    my @m = 1, 2;             my @w = 1, 2;
    @m ,= 3;                  @w = (@w, 3);
    check (@m.elems, @m[1]), (@w.elems, @w[1]), 'array: `@a ,= v` is `@a = (@a, v)`';
}
{
    my $m = 1;                my $w = 1;
    $m ,= 2;                  $w = ($w, 2);
    check ($m.elems, $m[1]), ($w.elems, $w[1]), 'scalar: `$x ,= v` is `$x = ($x, v)`';
}

# ---- and the plain comma is untouched --------------------------------------
{
    my @a = 1, 2,;
    check @a.elems, 2, 'a trailing comma in a list still ends it';
    my %h = (a => 1, b => 2);
    check %h.elems, 2, '…and a comma between pairs is still a separator';
    my @b = 1, 2;
    check (@b[0], @b[1]) eqv (1, 2), True, '…and `,` does not swallow a following =';
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
