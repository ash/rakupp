# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.11.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 5aa572bf, 6d2ee659, 606e98fe, e3b16b7e, 79314f20, 43558c4f, 82f6aba0, b084275a, a10df015, e46aa838, f54b0673, 23366987, 78bb0a72, fe5df52a, c78578b9, 72e3db13, cd3c0205.*

---

### New

### Faster

- **`my Int` variables cost what untyped ones do** (cd3c0205): a loop over
  `my Int` variables now runs as a kernel (one was refused as "a container a
  plain store would not honour"), and so does a loop with `div=`, `%=` or
  `mod=`. Interpreted, `my Int $x = …`, `$x = $x op …` and `$x += …` take the
  assignment lanes when the value is an Int. 2M rounds of
  `$z = $z + 3; $z = $z div 2` on a `my Int $z` went from 0.97 s to 0.012 s
  (Rakudo 0.26 s); 3M interpreted `my Int $e = 5` from 1.00 s to 0.22 s. A
  Kaprekar-number search written with `Int` types went from 1.93 s to 1.48 s
  (Rakudo 1.2-1.35 s). t/regression/typed-int-store-lanes.raku.

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
- **`[ h() ]` spreads the Hash a call returns** (78bb0a72) (#283): an
  array composer took a single Hash apart only for `[%h]`, `[{…}]` and
  `[%(…)]`; a Hash or Map returned by a sub or method call stayed one
  element, where Rakudo gives its pairs. A call's result now spreads too.
  `[$h]`, `[h(),]` and `[@r[0]]` are still one element. Code that relied
  on the old answer needs a trailing comma, as the Perl showcase did.
  t/regression/array-composer-spreads-returned-hash.raku.
- **`@$x` of an undefined scalar is one item** (fe5df52a) (#266): `my $x;
  @$x` was the empty list, so `for @$x { … }` never ran. It is `(Any,)`, as
  in Rakudo; a missing capture's `@<x>` is `(Nil,)`. `@(Empty)` is still
  empty. t/regression/list-of-undefined-scalar.raku.

### Fixed: crashes

- **`?self` on a type object in a role's method answers False** (c78578b9)
  (#269): with a `multi method Bool(R:D:)` in a role that does Iterable,
  `?self`, `so self` or `self ?? … !! …` on the class's type object died
  "No such method 't' for invocant of type 'Iterable'", naming the
  method that was running. A multi whose candidates do not fit now passes the
  call only to a parent's method of the same name.
  t/regression/bool-of-type-object-inside-role-method.raku.

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
- **An installed script parses knowing its module's exports** (23366987)
  (#270): `for ITEMS -> $n { … }` in a script installed with
  `rakupp install`, ITEMS a constant its own module exports, was a parse
  error unless the current directory had a `lib/` or the store was the
  default one. The script is now parsed on the search path, RAKULIB
  included, as a program given to rakupp directly is. t/install/run.raku.
- **`-c` refuses a variable a module declares but does not export**
  (72e3db13) (#282): `use Z; say $Q`, with Z's plain `our $Q`, was
  "Syntax OK" under `-c` and died X::Undeclared when run. A module with a
  `unit` package now supplies only what it marks `is export`. A module with
  no package of its own still supplies its `our` names, which Rakudo puts in
  GLOBAL. t/regression/check-only-unexported-module-variable.raku.

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
