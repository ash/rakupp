# Cookbook

Whole tasks, worked end to end. One page per task, each built around programs
that run — the files are here beside the page, so a recipe can be cloned and
run rather than copied out of prose.

This is the long-form shelf. Its neighbours are shorter: [RECIPES.md](../guide/RECIPES.md)
is one-liners by topic, [faq/](../guide/faq/) answers a question in a paragraph
and a caveat, [REFERENCE.md](../guide/REFERENCE.md) is the lookup sheet. A
recipe here is what you reach for when the question is "how do I actually do
this", and the answer is a program plus the four things that go wrong.

**Every program was run for the page it appears on**, against real services
where a recipe needs one, and the output shown is what it printed.

## Recipes

- **[dbiish.md](dbiish.md)** — reading and writing a table with DBIish,
  the same program against SQLite, MySQL and PostgreSQL. What differs between
  the three engines (two things), what does not (the placeholders), the three
  things that bite — `:database<$file>` silently not interpolating, `$sth.rows`
  answering 0 for a SELECT on SQLite, and values interpolated into SQL — and
  the three shapes a missing client library takes: a name with the paths the
  loader tried, an empty name where the driver probed and nothing matched, and
  an architecture mismatch between the interpreter and the library.
  Programs: [dbiish/](dbiish/).

- **[cli.md](cli.md)** — a word-frequency counter grown from a `sub MAIN` into
  a tool with options, a free usage message, a standard-input mode, subcommands
  by `multi MAIN`, and a single binary at the end. What bites: a named option
  after a positional is not seen, `--top 3` is not `--top=3`, a wrong type is a
  usage message rather than an error, and a `#|` that does not touch the routine
  documents nothing. Programs: [cli/](cli/).

- **[grammar.md](grammar.md)** — an nginx-shaped configuration file, nested
  blocks and all, parsed into data by a nine-line grammar and an actions class,
  and then three ways to tell the writer of a bad file which line is wrong —
  `.subparse`, a high-water mark in `ws`, and `|| <.panic(…)>` where the grammar
  is committed. What bites: `rule TOP` skipping nothing before its first atom, a
  panic in a candidate that was only being tried, `|` against `||`, and a token
  that never gives back what it ate. Programs: [grammar/](grammar/).

- **[http.md](http.md)** — a JSON API, with the server to talk to shipped
  alongside: GET and decode, POST and read back, and the four ways a call fails
  (no answer, 5xx, 4xx, a 200 that is not JSON) handled in one retry loop with a
  deadline. Plus HTTPS, and the two things to know about it here. What bites: 599
  is not a status a server sent, `HTTP::Tiny.new(:timeout(1))` does nothing, and
  a deadline does not cancel the request. Programs: [http/](http/).

- **[parallel.md](parallel.md)** — four requests in 2021 ms instead of 8020,
  primes 4.5× faster over eight promises, results in the order they arrive, a
  `Channel` worker pool, and eight threads sharing one `Array` — with what each
  one measured. What bites: `await` inside the loop, `race`/`hyper` not fanning
  out on this engine today, `$*THREAD.id` answering 1 everywhere, and an
  exception in a `start` block that waits for you. Programs: [parallel/](parallel/).

## Adding one

A recipe is a Markdown file here plus a directory of the programs it shows.
The page is the source of truth; [raku.online/cookbook/](https://raku.online/cookbook/)
is generated from it by `sites/cookbook/build.raku` in the
[raku.online](https://github.com/ash/raku.online) repository, which syncs these
files in and renders them. Run the programs before you publish the page — the
promise above is the only thing that makes a cookbook worth reading.
