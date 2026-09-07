# Longest-token matching — see the book

This was a short-form companion to the Internals book's chapter on
longest-token matching. It has been merged into that chapter, which is now the
single account: **[docs/book/ch/23-ltm.md](../book/ch/23-ltm.md)** (Chapter 23,
"Longest-Token Matching").

The two documents had come to hold the same ten sections, the same examples and
the same measured table, and neither named the other — so when the NFA became
the default ranker in v3.0.0, both were left describing the probe as the default
and only one of them was ever going to be found and fixed. Merging them is what
makes the next such change land once.

The resolver's own entry points, `namedRule` and `ltmResolve`, and the note that
a lexically scoped `<ws>` resolves at the call rather than the declaration, are
in the chapter's closing section.
