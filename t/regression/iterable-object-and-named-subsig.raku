# Regression: what IO::Glob's own suite needs once it stops dying early.
#   * an object that does Iterable answers the list methods Rakudo's Iterable
#     role supplies, by running its own .iterator
#   * assigning one to an `@` container iterates it
#   * a NAMED parameter may carry a sub-signature after its `!`/`?` marker
# The method split is Rakudo's and is irregular on purpose: `.list`, `.elems`,
# `.reverse`, `.join`, `.kv` on an Iterable object mean the invocant AS ONE ITEM.
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class Glob does Iterable {
    method iterator() { <c a b>.iterator }
}
my $g = Glob.new;

# --- the methods Iterable supplies: they ITERATE ----------------------------
ck $g.sort.List,   ('a', 'b', 'c'), '.sort iterates the object';
ck $g.map(*.uc).List, ('C', 'A', 'B'), '.map too';
ck $g.grep(* ne 'a').List, ('c', 'b'), 'and .grep';
ck $g.head, 'c', '.head is the first element';
ck $g.tail, 'b', '.tail is the last';
ck $g.first, 'c', '.first as well';
ck $g.unique.List, ('c', 'a', 'b'), '.unique';
ck $g.squish.List, ('c', 'a', 'b'), '.squish';
ck $g.Seq.List, ('c', 'a', 'b'), '.Seq';
ck $g.flat.List, ('c', 'a', 'b'), '.flat';

# The ones Iterable does NOT supply keep the Any meaning — the invocant as ONE
# item (`.list`, `.elems`, `.reverse`, `.join`, `.kv`). rakupp does not answer
# `.elems` on a bare object at all yet; that is a separate, pre-existing gap and
# is deliberately not asserted here.

# --- `@`-assignment iterates -----------------------------------------------
my @files = Glob.new;
ck @files.List, ('c', 'a', 'b'), 'assigning to an @ container iterates';
ck @files.elems, 3, 'so the elem count is the contents';

# a `for` loop was already right, and stays right
my @seen;
for Glob.new { @seen.push: $_ }
ck @seen.List, ('c', 'a', 'b'), 'a `for` loop still iterates';

# --- a named parameter with a sub-signature --------------------------------
sub required-named(:@spec! ($a, $b)) { "$a/$b" }
ck required-named(spec => [1, 2]), '1/2', 'a required named parameter destructures';
sub optional-named(:@spec ($a, $b)) { "$a-$b" }
ck optional-named(spec => [3, 4]), '3-4', 'and an optional one';
sub positional-sub(@a ($x, $y)) { "$x+$y" }
ck positional-sub([5, 6]), '5+6', 'the positional form is unchanged';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
