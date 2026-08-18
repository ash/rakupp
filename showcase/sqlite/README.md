# sqlite — a SQLite client over NativeCall

A working `sqlite3` client in which **nothing is reimplemented**. Every query is
executed by the real `libsqlite3`, reached through `is native('sqlite3')`; the
terminal is put into raw mode through the real `tcgetattr`/`tcsetattr`. There is
no C of our own anywhere — this is the showcase for talking to the outside
world.

Where the language showcases (lisp, js, perl, python) prove Raku++ can *host* a
language, this one proves it can *call* one: an existing C library, its opaque
handles, its out-parameter protocol, and all five of its storage classes,
marshalled in both directions.

## Usage

First make a database, or there is nothing for the rest of this to talk to.
The bundled seed builds three related tables — `artists`, `albums` and
`tracks` — and SQLite creates the file itself, so this is the whole setup:

```sh
build/rakupp showcase/sqlite/sqlite.raku demo.db - < showcase/sqlite/seed.sql
```

`.read` does the same thing from inside a session, which is handy once you
already have a database open:

```sh
build/rakupp showcase/sqlite/sqlite.raku demo.db '.read showcase/sqlite/seed.sql'
```

Everything below works on that `demo.db`:

```sh
build/rakupp showcase/sqlite/sqlite.raku demo.db 'select * from artists'
build/rakupp showcase/sqlite/sqlite.raku --format=csv demo.db '.tables'
build/rakupp showcase/sqlite/sqlite.raku demo.db - < script.sql   # a whole script
build/rakupp showcase/sqlite/sqlite.raku demo.db                  # no SQL → the browser
build/rakupp --exe -o sqlite showcase/sqlite/sqlite.raku && ./sqlite demo.db
```

| Option | What it does |
|---|---|
| `--format=table` | aligned columns with a rule (the default) |
| `--format=csv` | header row — byte for byte what `sqlite3 -csv -header` writes |
| `--format=json` | array of objects — matches `sqlite3 -json` |
| `--format=list` | pipe-separated, no header |
| `--readonly` | open the database read-only |
| `--create` / `--/create` | create the file if missing (on by default) |
| `--echo` | with `-`, print each statement before running it |

Options go before the database: the SQL argument is slurpy, so anything after
it — flags included — is taken as more SQL.

Dot commands, the ones a session actually reaches for: `.tables`, `.schema`,
`.read`, `.version`, `.help`.

Reading with `-` takes a real script, not a line at a time: statements are
gathered until `sqlite3_complete()` says the semicolon that ends one is not
inside a string, a comment or a `CREATE TRIGGER` body, so a `CREATE TABLE`
spread over ten lines arrives in one piece. A statement left unterminated when
the input runs out is run anyway, so `echo 'select 1' | … demo.db -` needs no
semicolon.

## The browser

With no SQL argument and a terminal on both ends, it opens a full-screen
browser on the alternate screen: tables on the left, rows on the right.

| Key | |
|---|---|
| arrows | move / scroll |
| Tab | switch pane |
| Enter | open the selected table |
| PgUp / PgDn, Home / End | page through rows |
| `:` | type SQL |
| `q` | quit |

Opening a *table* pages in SQL (`limit`/`offset`), so a million-row table costs
the same to open as a small one. A result typed at the `:` prompt is a different
matter — an arbitrary query cannot be paged that way, so its rows are
materialised and sliced in memory. The alternate screen and raw mode are
restored by `LEAVE` blocks, so the terminal survives even an exception.

## What the binding actually exercises

Thirty-three native declarations: twenty-eight `sqlite3_*` functions plus
`tcgetattr`, `tcsetattr`, `cfmakeraw`, `ioctl` and `isatty`. Keystrokes come
from `$*IN` — reading the tty through `read(2)` was a workaround for an engine
bug that is now fixed.

- **Opaque handles.** `sqlite3 *` and `sqlite3_stmt *` are Raku `Pointer`s,
  never inspected — exactly as the header intends.
- **Out-parameters.** `sqlite3_open_v2` and `sqlite3_prepare_v2` both write
  through a `sqlite3 **`, which is `Pointer is rw` here. This is the part of a
  binding most likely to be subtly wrong, which is why the compare below runs
  against the real client rather than trusting it.
- **All five storage classes.** `INTEGER` (64-bit, via `sqlite3_column_int64`),
  `FLOAT`, `TEXT`, `BLOB` and `NULL` are each marshalled to a Raku value and
  back. Blobs use `sqlite3_column_bytes` for the length rather than assuming a
  NUL terminator, because a blob may contain one.
- **Parameter binding.** `?` placeholders bind through the `sqlite3_bind_*`
  family, so values never go near string interpolation.
- **The statement lifecycle.** `prepare_v2` → `step` → `column_*` → `finalize`,
  with `reset` for re-execution, and errors read from `sqlite3_errmsg` and
  `sqlite3_extended_errcode`.

## The compare

```sh
RAKUPP=build/rakupp sh showcase/sqlite/compare.sh
```

Two oracles, which is one more than the other showcases get:

1. **Against the real thing.** The same queries through `sqlite3 -csv -header`
   and `sqlite3 -json`, diffed byte for byte. Those two formats are reproduced
   exactly on purpose: a difference means the binding or the value marshalling
   is wrong, and the real client says so immediately.
2. **Against the other engine.** The same program under Rakudo and under
   Raku++, in every output format, plus the dot commands and the error paths —
   the same check `modinfo` and `jsonreq` run.

Leg 1 is skipped when there is no `sqlite3` on `PATH`, leg 2 when there is no
`raku`. Queries live one per line, so a newline inside a value is written as
`char(10)` rather than typed literally.

## Requirements

`libsqlite3` must be present as a shared library — it ships with macOS and with
most Linux distributions (`libsqlite3-dev` or `sqlite-devel` where it does not).
Nothing needs compiling here; `is native('sqlite3')` finds it at runtime.
