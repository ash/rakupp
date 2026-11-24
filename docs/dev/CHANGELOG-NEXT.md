# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.11.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 5aa572bf, 6d2ee659, 606e98fe.*

---

### New

### Faster

### Fixed: silent wrong answers

- **A multidim subscript on a Match walks its captures** (6d2ee659):
  `$m{*;'name'}` answered `()` and `$m{'a';'c'}` answered `(Any)`. The
  walker knew Arrays and Hashes but not a Match. Both now answer what Rakudo
  answers, and so does `$m{*}`, which answered Nil instead of the named
  captures' values. LLM::Prompts' own tests use the first form, so
  `raku install LLM::Prompts` refused it; it installs now.
  t/regression/match-multidim-subscript.raku.

### Fixed: crashes

### Behaves like Rakudo

- **`print` at the REPL prompt shows its text** (5aa572bf) (#277): on a
  terminal the line editor drew its prompt starting with `\r`, over whatever
  `print "Hello"` had left without a newline, so the Hello vanished; `say`
  and `put` were fine. The prompt now starts on a new line when the output
  did not end one. A piped or `TERM=dumb` session is unchanged (`Hello> `,
  as in Rakudo). t/regression/repl-print-keeps-unterminated-line.raku drives
  a session on a pty through script(1).

### Platforms and packaging

### Tools

- **`--profile` tells multi candidates apart** (606e98fe): the candidates
  of a `multi` sub or method each get their own row, named with their
  signature (`multi quicksort($pivot, *@rest)`). Before, they all showed the
  same name, and two multi methods declared on one line shared a row. Every
  row now ends `file:line`. The JSON form adds `"line"` and, for a
  candidate, `"signature"`.

### Left open
