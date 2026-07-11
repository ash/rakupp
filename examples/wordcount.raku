#!/usr/bin/env raku
# Word-frequency analysis, the "wc | sort | uniq -c" of a single expression.
# A paragraph is lower-cased and chopped into words with a regex (so commas
# and full stops fall away), tallied into a `Bag` — Raku's built-in multiset,
# which counts occurrences for you — and the most common words are drawn as a
# little ASCII bar chart. Ties are broken alphabetically so the output is
# stable from run to run.

my $text = q:to/END/;
    The fog comes on little cat feet. It sits looking over harbour and city
    on silent haunches and then moves on. The fog is grey, the fog is soft,
    the fog is everywhere the city looks; and the little cat of the fog moves
    on and on over the harbour, over the city, and then the fog is gone.
    END

# Split into words: lower-case, then `.comb` out maximal runs of letters.
# Feeding that list to `.Bag` gives a multiset mapping word => count.
my $counts = $text.lc.comb(/ <[a..z']>+ /).Bag;

my $total  = $counts.total;
my $unique = $counts.elems;
say "Words: $total total, $unique unique.";
say '';

# Top ten, most frequent first (ties broken by the word itself). The bar is
# just the count rendered as that many '#'.
say "Most frequent words:";
for $counts.pairs.sort({ $^b.value <=> $^a.value || $^a.key leg $^b.key })[^10] -> $w {
    printf "  %-10s %2d  %s\n", $w.key, $w.value, '#' x $w.value;
}
