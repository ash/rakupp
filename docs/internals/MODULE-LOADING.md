# Module loading — see the book

This was a short-form companion to the Internals book's module chapters. It has
been merged into them:

- **[Chapter 32, Modules](../book/ch/32-modules.md)** — the search path, what a
  `use` does, and how imports are scoped.
- **[Chapter 33, The Installer](../book/ch/33-installer.md)** — the store's
  layout, resolution, and `rakupp install`.

The two were near-duplicates of those chapters, and all eleven of this file's
source line anchors had drifted. It also carried one claim the source
contradicts: that a `use` statement has no version requirement to consult.
`UseStmt::verReq` exists (`src/Ast.h:816`) and `pickInstalledDist` filters on
it, which is why `use Foo:ver<1.2+>` selects the newest satisfying install and
fails when none does.

The reviews that merged this file also found the boundary of that: the
`META6.json` `provides` fast path resolves a module without consulting
`verReq`, so a distribution mapped explicitly in its META loads whatever its
version. That is a bug rather than a design, and it is tracked separately.
