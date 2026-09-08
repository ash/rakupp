# What is implemented where — see the book

This was a short-form companion to the Internals book's classification chapter.
It has been merged into that chapter, which is now the single account:
**[docs/book/ch/03-classification.md](../book/ch/03-classification.md)**
(Chapter 3, "Classification").

The two held the same ten sections. What the short form had that the chapter did
not was four `src/Parser.cpp` line anchors, all of which had drifted onto
unrelated code, and a see-also list.

It was also, in one place, *ahead* of the book: it already recorded that `--aot`
serializes the AST rather than emitting one builder function per node, which the
book was still explaining the old way in two chapters. That is the argument for
merging in miniature — the correction had been made once, in the copy nobody was
reading, and the other copy went on being wrong for a month.
