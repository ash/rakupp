# `.sort` with a 1-ary KEY EXTRACTOR — a different contract from `.sort` with a
# 2-ary comparator, and the one that has an asymptotic trap in it. A key
# extractor is defined to run once per element; calling it inside the comparator
# instead runs it O(n log n) times, which is what Raku++ did until the
# Schwartzian transform landed (OPTIMIZATION.md, "`.sort($key)` extracts the key
# once per element" — 18.09 s to 0.68 s on the case that exposed it).
#
# sortnums.raku times bare `.sort`, which never calls back into Raku at all, so
# nothing here measured the callback contract. This kernel does: the key is
# cheap on purpose, because the cost being guarded is the CALL COUNT, not the
# key. 30k elements is ~30k key calls when the contract is honoured and ~450k
# when it is not.
my @words  = (1 .. 30_000).map({ "w" ~ ($_ * 2654435761 % 100_000) });
my @sorted = @words.sort(*.chars);
say @sorted.elems, " ", @sorted[0], " ", @sorted[*-1];
