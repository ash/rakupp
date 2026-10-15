# Regression: a list method over an ARRAY handed its elements out bare.
#
#   my @rows = [1, 2], [3, 4];
#   @rows.map({ my @y = $_; @y.elems })   # Rakudo: (1 1); rakupp: (2 2)
#
# Each element of an Array lives in a Scalar container, so whatever iterates
# one sees ITEMS: the topic a block gets, and what `.grep`, `.first`, `.sort`
# or `.head` hand back (`$[1, 2]`). A List's elements are bare, and `.List` is
# the one view of an Array that drops the containers. rakupp already had this
# for `for @rows { … }` and for `@rows[0]`; the list methods read the elements
# as plain values, and an index read itemized an Array but not a Hash.
#
# Found while making `$` parameters itemize (fca60078, 29ae99db). Every
# expectation was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku} want {$want.raku}" }
}
sub n($x) { my @y = $x; @y.elems }   # one element for an item, its elements otherwise

my @rows = [1, 2], [3, 4];
my @hs   = {a => 1}, {b => 2, c => 3};

# --- the topic a block gets -------------------------------------------------------
ck @rows.map({ my @y = $_; @y.elems }).List, (1, 1), '.map';
ck @hs.map({ my @y = $_; @y.elems }).List, (1, 1), '.map over Hashes';
ck @rows.grep({ my @y = $_; @y.elems == 1 }).elems, 2, '.grep';
ck @rows.first({ my @y = $_; @y.elems == 1 }).raku, '$[1, 2]', '.first';
ck @rows.flatmap({ my @y = $_; @y.elems }).List, (1, 1), '.flatmap';
ck ~@rows.classify({ my @y = $_; @y.elems }).keys, '1', '.classify';
ck ~@rows.categorize({ my @y = $_; @y.elems }).keys, '1', '.categorize';
ck @rows.sort({ my @y = $_; @y.elems }).elems, 2, '.sort with a mapper';
ck @rows.max({ my @y = $_; @y.elems }).raku, '$[1, 2]', '.max with a mapper';
ck @rows.unique(as => { my @y = $_; @y.elems }).elems, 1, '.unique :as';

# --- …and what comes back ----------------------------------------------------------
ck @rows.grep({ True }).map(*.raku).List, ('$[1, 2]', '$[3, 4]'), '.grep answers items';
ck @rows.head.raku, '$[1, 2]', '.head';
ck @rows.tail.raku, '$[3, 4]', '.tail';
ck @rows.reverse.map(*.raku).List, ('$[3, 4]', '$[1, 2]'), '.reverse';
ck @rows.Seq.map(*.raku).List, ('$[1, 2]', '$[3, 4]'), '.Seq';
ck @rows.sort({ $^a[0] <=> $^b[0] }).map(*.raku).List, ('$[1, 2]', '$[3, 4]'), '.sort with a comparator';
ck n(@rows.first({ True })), 1, '…so a list assignment of one takes it whole';

# --- lazy chains, read a prefix at a time -----------------------------------------
ck @rows.map({ my @y = $_; @y.elems }).first, 1, '.map(…).first';
ck @rows.grep({ True }).head.raku, '$[1, 2]', '.grep(…).head';

# --- what stays bare -----------------------------------------------------------
ck @rows.List.map({ my @y = $_; @y.elems }).List, (2, 2), '.List drops the containers';
ck ([1, 2], [3, 4]).map({ my @y = $_; @y.elems }).List, (2, 2), "a List's elements are bare";
{
    my $l = ([1, 2], [3, 4]);
    ck $l.map({ my @y = $_; @y.elems }).List, (2, 2), '…in a $-variable too';
}
ck @rows.map(*.elems).List, (2, 2), 'a method on the topic sees the Array';
ck @rows.map({ $_.elems }).List, (2, 2), '…written out';
ck @rows.raku, '[[1, 2], [3, 4]]', '.raku of the Array itself shows no items';
{
    my @copy = @rows.sort({ $^a[0] <=> $^b[0] });
    ck @copy.raku, '[[1, 2], [3, 4]]', '…nor of an Array filled from a sorted one';
}

# --- an index read itemizes a Hash as it did an Array -----------------------------
ck @rows[0].raku, '$[1, 2]', '@rows[0] (always was)';
ck @hs[1].raku, '${:b(2), :c(3)}', '@hs[1]';
ck n(@hs[1]), 1, '…so `my @z = @hs[1]` is one element';

# --- writes through the topic still reach the element -----------------------------
{
    my @m = [1], [2];
    @m.map({ .push: 9 }).eager;
    ck @m.raku, '[[1, 9], [2, 9]]', 'a mutation through $_ reaches the inner Array';
    my @i = 1, 2, 3;
    @i.map({ $_ *= 2 }).eager;
    ck @i, [2, 4, 6], '…and an assignment to $_ the element';
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
