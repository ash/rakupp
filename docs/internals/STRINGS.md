# Strings and the copy-on-write representation — see the book

This was a short-form companion to the Internals book's chapter on strings. It
has been merged into that chapter, which is now the single account:
**[docs/book/ch/09-strings.md](../book/ch/09-strings.md)** (Chapter 9,
"Strings"). The `Value` summary it opened with is Chapter 8.

The two documents had come to hold the same eleven sections in the same order,
and the short form's only unique content was two source line anchors that had
gone stale and a date stamp. Everything it said, the chapter said too —
including the things that were wrong in both, which is the argument for merging
rather than cross-linking: `StrBody` was 40 bytes in both and is 56, `mut()` was
the only write door in both and is not, and neither knew about the byte-offset
index tables that are the non-ASCII half of the string-scan story.
