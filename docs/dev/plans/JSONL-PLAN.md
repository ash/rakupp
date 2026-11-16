# Plan: `from-jsonl` and `to-jsonl` — JSON Lines in JSON::Native, at the parser's speed

**Status: Phases 0 and 1 done 2026-11-16** — the Raku reference and its
checks, then the engine primitives. On a 125 MB, 700,000-line file,
`from-jsonl` is 0.64 s against 1.40 s for the issue's Raku loop over
`rakupp-from-json`, and 0.62 s for the same records parsed as one document: the
loop costs ~5% over the parse. `to-jsonl` is 0.85 s against 1.9 s for
`map`/`join`, and the round trip is byte-identical.

Issue 268 asks for a call that parses a window of JSON Lines in one go —
bytes in, values out — so the loop sits where the parser already is. Measured
there on 100 MB / 700,000 lines (Raku++ 5.8.0): the codec is ~1.10 s, and the
Raku-level work around it (decode, split, the blank test, the push) is about as
much again. 700,000 separate `rakupp-from-json` calls cost the same as one call
over the lot, so the per-call boundary is free and only the loop is left to
move.

JSON::Native grows two names, beside `from-json` and `to-json`, answered the
way those are: by the engine's codec on Raku++, and by each engine's existing
path elsewhere — Rakudo's built-in parser and the `nqp::` writer, JSON::Fast on
mutsu, `JSON::Native::Raku` everywhere. No C extension: the parse is already
engine C, and on Raku++ the engine answers `use JSON::Native` itself, so an
extension would be bypassed.

Nothing in the ecosystem has this call. `JSONL` (zef:apogee) is a path-based
reader that errors on blank lines unless `:lenient`; Qwiratry's NDJSON format
takes text and skips blank lines. The name `from-jsonl` is free.

## The contract

Each function is defined by a Raku expression it must equal, so every path is
tested against one oracle:

```raku
from-jsonl($x, :$immutable)  ≡  lines of $x, blank ones dropped, each through from-json(:$immutable)
to-jsonl(@v, *%o)            ≡  @v.map({ to-json($_, :!pretty, |%o) ~ "\n" }).join
```

**`from-jsonl(Blob|Str $x, :$immutable)`**

- A `Blob` is decoded as UTF-8; a `Str` is taken as it is.
- Lines are what `Str.lines` gives: split at `\n`, `\r\n` or a lone `\r`, on
  Rakudo and Raku++ alike, so CRLF input needs no normalising. A last line
  without a terminator is a line; a final terminator makes no empty one. (JSON
  allows no raw CR or LF inside a string, so the split never cuts a valid
  document.)
- A line holding nothing but spaces and tabs is blank and yields nothing. A
  line of U+00A0 is not blank (it is malformed), where `.trim` would call it
  blank.
- Every other line is `from-json($line, :$immutable)`, JSON::Fast-exact.
- The result is an `Array`, or a `List` under `:immutable`.
- A line `from-json` refuses throws `X::JSON::Native::Line`: `.line` (1-based,
  counting blank lines), `.exception` (what `from-json` threw), message
  `line N: <that exception's message>`.
- Other adverbs are refused, as `from-json` refuses unknown ones. (`:allow-jsonc`
  is not offered: a `//` comment is a line of its own and makes no sense here.)

**`to-jsonl(@values, :$sorted-keys, :$enums-as-value)`**

- One compact `to-json` per value, each followed by `\n`; an empty list is `""`.
  Compact JSON escapes `\n` inside strings, so a value is always one line.
- `:pretty`, `:spacing` and `:level` are refused — JSON Lines has one layout.
- The result is a `Str`; the caller encodes and writes it.

## Phases

### Phase 0 — the reference, and the tests that pin it

The pure-Raku `from-jsonl` / `to-jsonl` in `JSON::Native::Raku`
(`rakulib/JSON/Native/Raku.rakumod`, kept byte-identical to the
distribution's copy) are the contract made executable: every faster path is
checked against them, and they are what the engine falls back to. With them,
`X::JSON::Native::Line`, declared outside the module as
`X::JSON::Native::AdditionalContent` is.

`t/regression/json-lines.raku` pins the edge cases: blank and
whitespace-only lines, CRLF, no final newline, an empty window, a malformed line
and its number, `:immutable`, a Blob with non-ASCII and non-NFC text, `to-jsonl`
of an empty list, a string holding `\n`, `:sorted-keys`, a refused `:pretty`.

### Phase 1 — engine primitives (`src/Builtins.cpp`, `src/BuiltinsRegister.cpp`)

`jsonFromJsonlBody` / `jsonToJsonlBody`, with a per-line / per-value
`JsonUncovered` hand-over like the existing bodies. One C++ pass per window:

- a `Blob` gets one UTF-8 validation and one NFC quick check over the whole
  buffer — the parser assumes NFC source text, which a decoded `Str` always is;
  pure ASCII skips both, a failing check normalises the buffer once;
- `memchr` for line ends, the blank test in the same scan;
- `jsonParseValue` takes a `std::string_view`, so each line is parsed in place:
  no `Str` per line, and no parse runs past its line's end;
- the result array is reserved to the line count;
- `to-jsonl` runs `jfEncode` in a loop into one string.

Builtins: `rakupp-from-jsonl`, `rakupp-to-jsonl`, and `rakupp-json-codec`
learns `'from-jsonl'` / `'to-jsonl'`.

**As built.** The fallback is handed the WHOLE call, not the refused line:
it is JSON::Native::Raku's `from-jsonl` / `to-jsonl`, which numbers the line
and raises `X::JSON::Native::Line` itself, so the engine never has to build
that class. For valid JSON the codec declines nothing, so a window is
re-parsed in Raku only on its way to an error. A Blob is taken as it is when
it is pure ASCII (one OR-reduction over the bytes); otherwise `.decode` itself
runs, so its errors and its NFC are the ones a program would get. The bare
primitives raise X::AdHoc with the line and column (`from-jsonl: malformed
JSON at line 3, column 4`). `:enums-as-value` is handed over, as `to-json`'s
codec already hands it over. The bodies are in `BuiltinsJson.cpp`;
`Builtins.cpp` (30 lines under its ceiling) gained only `jsonWriteLines`,
the 9-line loop over the static writer.

**For Phase 2:** `src/InterpreterModules.cpp` is exactly at the default
10,000-line ceiling, so its edits must be net-zero or move code out.

### Phase 2 — the engine answers `use JSON::Native` with them (`src/InterpreterModules.cpp`)

- Both names into the `json` row of `kDataNativeTags`; the declaration checker
  reads that table, so `Parser.cpp` (pinned at its ceiling) is not touched.
  `use Data::Native <json>` hands them out too.
- Both into `jsonNames`, and exact routines with the `JSON::Native::Raku`
  fallback (a variant of `jsonExactRoutine`).
- **JSON::Native's `interfaceVer` to 0.2.0**, in step with the distribution:
  an installed distribution newer than it takes over from the engine, so
  shipping 0.2.0 without the bump would quietly put Raku++ on the slow path.
- `--exe` embeds the fallback as it does now; run `t/exe/parity.raku`.

### Phase 3 — JSON::Native 0.2.0 (`~/raku-modules/JSON-Native`)

- Raku++: probe `rakupp-json-codec('from-jsonl', …)` and export the routine
  with no Raku frame in front, as `from-json` is; older engines get the Raku
  path.
- Rakudo: an `nqp::` line scan feeding `Rakudo::Internals::JSON.from-json`;
  `to-jsonl` on the `JSON::Native::Rakudo` writer.
- mutsu: JSON::Fast per line. Elsewhere: `JSON::Native::Raku`.
- `EXPORT`, `%IMPL`, the README, `t/06-lines.t` over every backend available.
- The issue author's `json-each` is theirs: we offer the primitive, not take
  the reader.

### Phase 4 — measure

The issue's file shape (100 MB, 700k lines). Target: `from-jsonl` within ~10%
of the parse alone (~1.1 s), against the ~1.65 s of the best Raku-side loop.
`to-jsonl` against the `map`/`join` one-liner. Levers held back until a profile
asks for them: reusing hash keys across records (JSONL repeats them every
line), and splitting one window across threads inside the pass.

### Phase 5 — docs and the reply

Rows in `docs/internals/JSON.md`'s map, `DATA-NATIVE.md`'s names, the
changelog, the distribution's README; a drafted reply to issue 268 with the
numbers, for the maintainer to post.

**Gates:** Roast, then `t/run.raku`; the distribution's suite on Raku++ and
Rakudo.
