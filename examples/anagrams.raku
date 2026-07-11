#!/usr/bin/env raku
# Anagram classes. Two words are anagrams when they are the same multiset of
# letters, so the trick is to give every word a canonical key — its letters
# sorted into order — and let words with the same key fall into the same
# bucket. `.classify` does exactly that grouping; the rest is string ops
# (`.comb.sort.join`) and a deterministic sort of the buckets for printing.

my @words = <
    listen silent enlist tinsel   evil live vile veil
    stop tops pots spot opts post  night thing
    cat act tac                    angle glean angel
    dog god                        rat tar art
    cab bac                        stressed desserts
>;

# The canonical key: sort a word's letters. "listen" and "silent" both map
# to "eilnst", so `.classify` drops them into the same group.
my %groups = @words.classify(*.comb.sort.join);

say "Anagram groups (largest first):";
say '';

# Keep only the real anagram groups (two or more words), and print them
# biggest-first, breaking ties by key so the output is always the same.
for %groups.pairs.sort({ $^b.value.elems <=> $^a.value.elems
                         || $^a.key leg $^b.key }) -> $group {
    next if $group.value.elems < 2;
    printf "  %-9s  %s\n", $group.key, $group.value.sort.join(' ');
}

say '';
my $classes = %groups.pairs.grep(*.value.elems >= 2).elems;
say "Found $classes anagram classes among {@words.elems} words.";
