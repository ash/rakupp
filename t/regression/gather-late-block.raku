# Regression: a gather's block runs LATE — when something pulls, on a stack of
# its own (ROAST-TRACKS-PLAN B1) — and three things that assumed it ran at
# once broke with that, all found by running the module battery's own test
# suites (2026-09-27):
#
# - `samewith` inside the block lost the routine that wrote the gather (the
#   redispatch stack is per stack): Digest's SHA-3 is
#   `multi Keccak(…) { gather for samewith … { … } }`;
# - an object whose `.iterator` hands out a gather's iterator (IO::Glob's
#   `method iterator { self.dir.iterator }`) iterated as NOTHING: `for` and
#   `.sort` read the iterator's buffer, which a gather fills only when pulled;
# - and, older than B1 but in the same place: a placeholder in a `for` block
#   inside a gather (`gather { for <a b> { take $^v } }`) was claimed by the
#   gather's own block, which then wanted an argument.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    multi squares(Str $s) { samewith $s.comb.map(*.ord) }
    multi squares(@codes) { gather for samewith @codes.elems { take $_ * $_ } }
    multi squares(Int $n) { ^$n }
    ck(squares('ab').List, (0, 1), 'samewith inside a gather reaches the routine that wrote it');
}

{
    class G does Iterable {
        method dir() { my @open = 1, 2, 3; gather while @open { my $x = @open.shift; next if $x == 2; take $x } }
        method iterator(G:D:) { self.dir.iterator }
    }
    my @sorted = G.new.sort;
    ck(@sorted, [1, 3], '.sort of an object whose iterator is a gather\'s');
    my @seen;
    for G.new -> $x { @seen.push: $x }
    ck(@seen, [1, 3], '…and a `for` over it');
    ck(G.new.list.elems, 1, '(.list of it is the object itself)');
}

{
    ck((gather { for <a b c> { take $^v } }).List, <a b c>, 'a `for` block\'s placeholder is its own inside a gather');
    ck((gather for <a b c> { $^v.uc andthen $v.take orelse .say }).List, <a b c>,
       '…with andthen/orelse thunks reading it');
    sub no-args() { my @o; for 1..2 { @o.push: $^x } ; @o }
    ck(no-args(), [1, 2], '…and inside a routine that takes nothing');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
