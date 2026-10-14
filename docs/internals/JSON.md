# JSON in the engine — one parser, one writer, and the ways in

Raku++ has one JSON parser and one JSON writer, both in C++: `jsonParseValue`
in [src/BuiltinsJson.cpp](../../src/BuiltinsJson.cpp) and `jfEncode` in
[src/Builtins.cpp](../../src/Builtins.cpp). Everything in the engine that
reads or writes JSON reaches them through one of the names below. The names
are many; the code behind them is not.

## The map

| way in | what it is | who it is for | since |
|---|---|---|---|
| `use JSON::Native` | a module name the engine answers itself (`dataNativeUse`) | **programs** | v3.26.0; exact, with nothing installed, since v5.4.0 |
| `use Data::Native <json>` | the same `from-json` and `to-json` under Data::Native's `json` tag | programs | v3.26.0 |
| `use JSON::Fast` | the installed module, its `from-json` and `to-json` wrapped (`wrapJsonFastExports`) | unmodified programs | v3.7.0 |
| `rakupp-from-json`, `rakupp-to-json` | builtins, found with `&::('rakupp-from-json')` | `::Native` distributions probing the engine ([DATA-NATIVE.md](../guide/DATA-NATIVE.md#the-rakupp--names)) | v3.26.0 |
| `rakupp-json-codec` | a builtin that returns the codec as a routine with a fallback | JSON::Native | v5.4.0 |
| `Rakupp::Internals::JSON` | a class the engine answers, with `.from-json` and `.to-json` | Raku++'s own tooling: `tools/install.raku`, `tools/upgrade.raku` | v3.5.0 |
| `Rakudo::Internals::JSON` | the same class under Rakudo's name | zef, OpenSSL, Pakku | v1.1.5 |
| `jsonParseDoc`, `jsonEncode` | C++ functions, no Raku name | the engine reading META6 files and writing its store's `dist/` records | — |

Which path answered can be asked: `json-backend`, after `use JSON::Native`,
says `engine`; `rakupp-json-backend` says `core`.

## Reading: what happens where the parser stops

What each name does with a call the parser cannot answer — an option it lacks,
a value outside JSON, a document it refuses — is most of why they are separate:

- **`use JSON::Native`, `use Data::Native <json>`** hand it to
  `JSON::Native::Raku`, a pure-Raku port of JSON::Fast that ships inside the
  binary ([rakulib/JSON/Native/Raku.rakumod](../../rakulib/JSON/Native/Raku.rakumod))
  and is loaded by the first call that needs it. So the answer is always
  JSON::Fast's.
- **`use JSON::Fast`** hands it to JSON::Fast's own code.
- **`rakupp-json-codec`** hands it to the routine the caller passed.
- **`rakupp-from-json`, `rakupp-to-json`** have nothing behind them, so they
  raise, with the position: `[1,` fails with
  `from-json: malformed JSON at byte 3 (line 1, column 4)`.
- **`Rakupp::Internals::JSON.from-json`** takes no options and raises a bare
  `Invalid JSON`.
- **`jsonParseDoc`** is lenient on purpose: content after the document is
  ignored, because META files are trusted input.

## Writing: one writer, two sets of defaults

Every `to-json` above runs `jfEncode`, which writes JSON::Fast's text byte for
byte. They differ only in their defaults:

- **Everything a `use` hands out, and the `rakupp-` names**, take JSON::Fast's:
  pretty-printed unless `:!pretty`.
- **`Rakupp::Internals::JSON.to-json`** takes Rakudo's for that method, since
  zef and Rakudo's own installer are its callers: one line unless `:pretty`,
  with `:spacing`, `:level` and `:sorted-keys` honoured and any other option
  ignored. On plain data the text is what Rakudo writes. Outside it — a Set,
  an enum, an object — it is JSON::Fast's, not that of the older writer Rakudo
  carries.
- **`jsonEncode`** is one line in insertion order, for the store's records. A
  value JSON cannot hold goes in as its `Str`, rather than stopping an install
  half-way.

## Which one to use

- **A program:** `use JSON::Native`. On Raku++ 5.4.0 and newer it needs
  nothing installed; it runs unchanged on Rakudo and mutsu, and gives
  JSON::Fast's answers byte for byte.
- **A distribution that wants the engine's speed and still runs everywhere:**
  depend on JSON::Native, or probe `&::('rakupp-from-json')` behind a fallback
  of your own, as [DATA-NATIVE.md](../guide/DATA-NATIVE.md) describes.
- **Toolchain code that cannot have dependencies** — an installer:
  `::('Rakupp::Internals::JSON')`, probed by calling it, since a type object is
  undefined and `//` would always reject it. Nothing else: a name like this,
  copied into ordinary modules, is how `Rakudo::Internals::JSON` became
  load-bearing.
- **Never** write `Rakudo::Internals::JSON` in new code. It is answered so zef
  runs.

## Why there are so many

Each name was added for a caller the earlier ones could not serve, and none
was retired, because something released still looks it up — JSON::Native
0.1.0 probes all three `rakupp-` names. A portable module may not mention
anything Rakudo cannot compile, so every hook into the engine is a string
looked up at run time, and its shape — a class, a sub, a routine with a
fallback — is part of its contract. When a new shape was needed, a new name
was safer than changing the old one under released modules.

## One file, two copies

`rakulib/JSON/Native/Raku.rakumod` is a copy of
`JSON-Native/lib/JSON/Native/Raku.rakumod` in
[ash/raku-modules](https://github.com/ash/raku-modules). The two must be kept
equal: change one, copy it to the other, and raise the interface version the
engine claims for JSON::Native (0.1.0) when the surface changes. The engine
steps aside for an installed JSON::Native newer than that, for one on `-I`,
and when `JSON_NATIVE_BACKEND` names another path.

## See also

- [RAKUDO-INTERNALS.md](../dev/ecosystem/RAKUDO-INTERNALS.md) — who calls
  `Rakudo::Internals`, and the two-name policy.
- [DATA-NATIVE.md](../guide/DATA-NATIVE.md) — the `rakupp-` names and when the
  engine answers a `use`.
- [faq/http.md](../guide/faq/http.md) — why JSON is the one network format the
  engine carries.
- JSON::Native's [design log](https://github.com/ash/raku-modules/blob/main/notes/JSON-Native.md).
