# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.11.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 5aa572bf.*

---

### New

### Faster

### Fixed: silent wrong answers

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

### Left open
