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
  the three engines (two things), what does not (the placeholders), and the
  five traps: `:database<$file>` silently not interpolating, `$sth.rows`
  answering 0 for a SELECT on SQLite, values interpolated into SQL, an
  architecture mismatch between the interpreter and the client library, and a
  driver that reports its library missing on the second line.
  Programs: [dbiish/](dbiish/).

## Adding one

A recipe is a Markdown file here plus a directory of the programs it shows.
The page is the source of truth; [raku.online/cookbook/](https://raku.online/cookbook/)
is generated from it by `sites/cookbook/build.raku` in the
[raku.online](https://github.com/ash/raku.online) repository, which syncs these
files in and renders them. Run the programs before you publish the page — the
promise above is the only thing that makes a cookbook worth reading.
