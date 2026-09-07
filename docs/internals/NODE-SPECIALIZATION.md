# Node specialization — see the book

This was a short-form companion to the Internals book's chapter on node
specialization. It has been merged into that chapter, which is now the single
account: **[docs/book/ch/19-node-specialization.md](../book/ch/19-node-specialization.md)**
(Chapter 19, "Node Specialization").

Eighty-three of this file's two hundred non-blank lines were byte-identical with
the chapter. Where the two differed they differed by being out of date, in all
four places — including on the claim the chapter builds its argument from, that
a specialized node still looks its variable up by name, which lexical pads
stopped being true. The measured non-`-O` residual-cost table, which was the one
thing that lived only here, is in the chapter's "Codegen already had this"
section.
