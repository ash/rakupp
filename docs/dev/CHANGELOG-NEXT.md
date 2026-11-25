# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.11.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 5aa572bf, 6d2ee659, 606e98fe, e3b16b7e, 79314f20, 43558c4f, 82f6aba0, b084275a, a10df015, e46aa838, f54b0673.*

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

- **`$*KERNEL.total-memory`, `.free-memory` and `.cpu-usage` answer Ints**
  (e3b16b7e) (#279): all three answered the kernel's name, `linux` or
  `darwin`. They now give physical and free memory in bytes and the
  process's CPU time in microseconds, read as Rakudo reads them (libuv's
  sources per platform, getrusage). t/regression/kernel-memory-cpu.raku.
- **`%Foo::BAR` / `@Foo::BAR` naming no symbol read as Any** (79314f20)
  (#274): they read as a new defined empty Hash or Array, so
  `%Foo::BAR // 5` answered `{}`. `%OUR::BAR` died with X::Undeclared; it
  answers Any too. t/regression/qualified-sigil-miss.raku.
- **Quotes inside a chained `<…>` subscript are part of the key**
  (82f6aba0) (#285): `%a<a><'b-c'>` read the key `b-c`; it reads `'b-c'`,
  as the first subscript and Rakudo do. `f <a 'b c' d>` in argument
  position gave three words; it gives Rakudo's four.
  t/regression/chained-angle-quoted-key.raku.
- **`my foo_t $x` keeps a lowercase type name** (b084275a) (#280): a
  type was recognised by its capital letter, so `foo_t` was taken for a
  native and dropped: `$x` started as Any, took any value, and
  `my foo_t $x .= new` called .new on Any. Cairo's matrix declarations died
  there, and `raku install Gnome::Gtk4` with them. A lowercase name that is
  no native is a type now, in `$`, `@` and `%` declarations alike.
  t/regression/lowercase-type-in-my.raku.
- **A Proxy returned by a call is read once, at the assignment**
  (a10df015) (#281): `my $a = $obj.m`, with an `is rw` method returning a
  Proxy, stored the Proxy, so every read of `$a` ran FETCH again. A CStruct
  built in FETCH kept changing under the copy: Cairo's
  `my $prev = $ctx.matrix` followed the matrix it was meant to save.
  t/regression/proxy-returned-by-call-fetched-once.raku.

### Fixed: crashes

### Behaves like Rakudo

- **Parameter traits take `<…>`, `{…}` and `[…]` arguments, and MAIN asks
  a module's ARGS-TO-CAPTURE** (e46aa838) (#284): `Bool :$v is option<!>` was
  a parse error, so App::Prove6's `prove6` could not start. With that
  fixed, Getopt::Long's `--no-v` still printed the usage: the auto-invoked
  MAIN parsed `@*ARGS` itself and never called the ARGS-TO-CAPTURE the
  module exports. It does now, as an explicit RUN-MAIN already did.
  t/regression/param-trait-unparenthesised-arg.raku.
- **A subset of an imported class refines that class** (f54b0673) (#278):
  `subset NotLoggedIn of UserSession`, with UserSession imported from a
  module, gave its parameters a type that named no class, so
  `.type ~~ SomeRole` was False. Cro finds a route's auth parameter that way
  and refused the route; `of Login::UserSession` worked. Both spellings do
  now. t/regression/subset-of-imported-class-param-type.raku.

- **`print` at the REPL prompt shows its text** (5aa572bf) (#277): on a
  terminal the line editor drew its prompt starting with `\r`, over whatever
  `print "Hello"` had left without a newline, so the Hello vanished; `say`
  and `put` were fine. The prompt now starts on a new line when the output
  did not end one. A piped or `TERM=dumb` session is unchanged (`Hello> `,
  as in Rakudo). t/regression/repl-print-keeps-unterminated-line.raku drives
  a session on a pty through script(1).
- **Path methods on values that are not paths are missing methods**
  (43558c4f) (#275): `42.add('x')` was the path `42/x`, and `.open` on a
  user object opened a file named after its address. `add`, `parent`,
  `basename`, `open` and the other IO::Path methods on Int, Rat, Bool, List,
  Hash, Pair, code or a plain object now raise X::Method::NotFound, as in
  Rakudo. A Str still works as a path.
  t/regression/path-methods-on-any-value.raku.

### Platforms and packaging

### Tools

- **`--profile` tells multi candidates apart** (606e98fe): the candidates
  of a `multi` sub or method each get their own row, named with their
  signature (`multi quicksort($pivot, *@rest)`). Before, they all showed the
  same name, and two multi methods declared on one line shared a row. Every
  row now ends `file:line`. The JSON form adds `"line"` and, for a
  candidate, `"signature"`.

### Left open
