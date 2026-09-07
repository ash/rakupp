# Method dispatch — see the book

This was a short-form companion to the Internals book's dispatch chapters. It
has been merged into them:

- **[Chapter 16, Method Dispatch](../book/ch/16-dispatch.md)** — the interpreter's
  ladder, the method cache, roles and the MRO.
- **[Chapter 28, Dispatch in Compiled Code](../book/ch/28-dispatch-compiled.md)** —
  the four ways a compiled program reaches a routine, and the fast paths.

Chapter 28 is the closer match — twenty-one of this file's lines were identical
with it, against five with chapter 16 — and it was already correct in the two
places this file was wrong: the count of named builtins on the fast path, and
where the dispatch table now lives. It is no longer one file: the table is split
across `src/MethodCallPart2.cpp`, `src/MethodCallPart3.cpp`,
`src/MethodCallTail.cpp` and `src/MethodCallSegment.h`.
