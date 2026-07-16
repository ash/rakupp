# json — a JSON parser & formatter

Reads JSON into native Raku values with a grammar, and writes them back out —
pretty-printed or minified — with an optional `jq`-style path to pull one value.
Where [lisp](../lisp/) and [forth](../forth/) implement whole languages, this is
the everyday job a grammar is for: read a data format, do something, write it
back.

(There's a smaller [`examples/json.raku`](../../examples/json.raku) that only
parses to a data structure; this one round-trips and queries.)

## Run it

```sh
build/rakupp showcase/json/json.raku showcase/json/sample.json          # pretty-print
build/rakupp showcase/json/json.raku --compact showcase/json/sample.json # minify
build/rakupp showcase/json/json.raku --query='.users[0].name' showcase/json/sample.json
cat data.json | build/rakupp showcase/json/json.raku                     # from stdin
# or compile a standalone binary:
build/rakupp --exe -o json showcase/json/json.raku
```

## Examples

Pretty-printing sorts object keys, so the output is stable (nice for diffs):

```
$ echo '{"b":2,"a":[1,2],"c":{"x":true}}' | build/rakupp showcase/json/json.raku
{
  "a": [
    1,
    2
  ],
  "b": 2,
  "c": {
    "x": true
  }
}
```

Minify and query:

```
$ build/rakupp showcase/json/json.raku --compact showcase/json/sample.json
{"escapes":"a \"quote\", ...","maintainer":null,"name":"rakupp", ... }

$ build/rakupp showcase/json/json.raku --query='.users[0].name' showcase/json/sample.json
"Ada"
```

## What it handles

- **Objects, arrays, strings, numbers, `true`/`false`/`null`** — objects become
  Hashes, arrays Arrays, scalars `Str`/`Int`/`Num`/`Bool`, `null` a sentinel.
- **String escapes** both ways: `\" \\ \/ \n \t \r \b \f` and `\uXXXX` (so
  `"café ❤"` decodes to `café ❤` and re-encodes cleanly).
- **Numbers**: integers stay `Int`, decimals and exponents become `Num`.
- **`--query`**: a path of `.key` and `[index]` steps, e.g. `.users[0].name`.

## How it works

- **Grammar** — a `proto rule value {*}` with one `:sym<…>` alternative per JSON
  value kind (object / array / string / number / true / false / null); the actions
  class reduces each match into a native Raku value as it parses.
- **Escape decoding** is a single left-to-right pass (sequential global substs
  would corrupt an escaped backslash followed by a letter).
- **Serializer** — a recursive `to-json` walks the value: objects and arrays
  indent by depth (or collapse under `--compact`), strings are re-escaped, and
  every scalar is formatted for valid JSON.
