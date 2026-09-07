# The front end — see the book

This was a short-form companion to the Internals book's front-end chapters. It
has been merged into them, and they are now the single account:

- **[Chapter 4, The Lexer](../book/ch/04-lexer.md)**
- **[Chapter 5, The Parser](../book/ch/05-parser.md)**
- **[Chapter 6, User-Defined Operators](../book/ch/06-user-operators.md)**
- **[Chapter 7, The AST](../book/ch/07-ast.md)**

About 480 of this file's 590 lines were those chapters in shorter words, and ten
of its twelve code blocks were the book's blocks — including the mistakes in
them. The review that merged the two found seven findings that applied
identically to both copies: the same removed lexer field quoted in both, the same
superseded explanation of how a novel operator survives lexing, the same stale
limitations. Two copies of one explanation drift together and are fixed once, if
you are lucky.

What lived only here has been lifted into the chapters: the `langRev_` paragraph
on how `use v6.X` gates 6.e syntax — which is also the one place the two files
flatly contradicted each other, and the short form was right — the note on
combining marks in `consumeIdentChars`, and the supported/not-supported summary.
