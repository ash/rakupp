# A self-referential container renders as a CYCLE, not as nested copies of
# itself — issue #85's neighbour.
#
# `.raku` has detected this all along (rakuRepr keeps a set of the containers it
# is inside and prints `[...]` / `{...}`). `.gist` and `.Str` had only a depth
# backstop, so they rendered 512 levels of nesting before giving up: `say @a` on
# a two-element cycle printed a little over two kilobytes of brackets. Easy to
# reach now that `,=` works, because `@a ,= 3` builds exactly such an array —
# on Rakudo as well, which names the cycle rather than nesting it.
#
# Passes under both rakupp and Rakudo. The two spell the marker differently
# (`[[...] 3]` here, `(\Array_… = [Array_… 3])` there), and the wording is not
# the point: what both must do is terminate, stay short, and say it once.
#
# `.Str` gets the same guard and is checked in t/fixtures/native-parity.raku
# instead of here: Rakudo HANGS on `@a.Str` for this value — its gist and its
# .raku both detect the cycle, its Str does not — so asserting it in a file
# both engines run would hang the suite rather than fail it.

my $fails = 0;
sub check($got, $want, Str $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "not ok - $desc"; note "GOT [{$got.raku}] WANT [{$want.raku}]" }
}

{
    my @a = 1, 2;
    @a[0] = @a;                       # @a's first element IS @a
    check @a.gist.chars < 100, True, 'the gist of a cyclic array is bounded';
    check @a.raku.chars < 100, True, '…and so is its .raku';
    check @a.elems, 2, '…while the array itself is still two elements';
}

{
    my %h = a => 1;
    %h<s> = %h;
    check %h.gist.chars < 100, True, 'the gist of a cyclic hash is bounded';
    check %h.raku.chars < 100, True, '…and so is its .raku';
    check %h.elems, 2, '…while the hash itself is still two keys';
}

# …the same value `,=` produces, which is how this was found
{
    my @a = 1, 2;
    @a ,= 3;
    check @a.gist.chars < 100, True, '`@a ,= 3` builds one, and it renders bounded';
    check @a.elems, 2, '…and is the two-element list it should be';
}

# and nothing acyclic changed
{
    my @n = [1, [2, [3, [4]]]];
    check @n.gist, '[1 [2 [3 [4]]]]', 'ordinary nesting still renders in full';
    my %d = a => { b => { c => 1 } };
    check %d.gist, '{a => {b => {c => 1}}}', '…and so does a nested hash';
    my @twice = [1, 2];
    my @both = @twice, @twice;        # the SAME container twice is not a cycle
    check @both.gist, '[[1 2] [1 2]]', 'the same container in two places is not a cycle';
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
