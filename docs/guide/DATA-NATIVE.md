# `Data::Native` — one portable `use` for what the engine already does

```raku
use Data::Native;

say to-json({ ok => True });            # JSON
say from-csv("a,b\n1,2\n", :headers);   # CSV
say sha256-hex('abc');                  # digests and HMAC
say uncompress(compress($blob));        # zlib / gzip / raw deflate
say crypt_random_buf(32);               # bytes from the OS CSPRNG
```

Nothing to install. On Raku++ the compiler answers that `use` from its own
built-ins: no file is read, no module is loaded, no dependency is resolved. On
any other engine the same line loads a distribution of the same name that
composes the ecosystem's usual modules, so the program is portable rather than
Raku++-only — which is the point. A program written against this runs on Rakudo,
it just runs slower there.

## The five tags, thirty-two names

`use Data::Native` brings all of them. `use Data::Native <json csv>` brings only
those tags, and an unknown tag is an error naming the tag rather than a silent
no-op.

| tag | exports |
|---|---|
| `json` | `to-json` `from-json` `json-backend` |
| `csv` | `from-csv` `to-csv` `csv-backend` |
| `digest` | `md5` `sha1` `sha224` `sha256` `sha384` `sha512`, their `-hex` twins, `hmac` `hmac-hex` `digest-backend` |
| `zlib` | `compress` `uncompress` `gzslurp` `gzspurt` `crc32` `adler32` `zlib-backend` |
| `random` | `crypt_random_buf` `crypt_random` `crypt_random_uniform` `random-backend` |

**Every signature is an existing module's, character for character**, so this is
a one-line edit in either direction:

| tag | the interface it is |
|---|---|
| `json` | `JSON::Fast` |
| `csv` | `CSV::Native` (there is no other; `Text::CSV` is a different surface) |
| `digest` | `Digest::MD5` / `Digest::SHA1` / `Digest::SHA2` for the bare names, bduggan's `Digest::SHA*::Native` for the `-hex` spelling, `Digest::HMAC` for the MAC |
| `zlib` | `Compress::Zlib` |
| `random` | `Crypt::Random` |

Where the tag is a **superset**, it is additive and written down:

- `:gzip` and `:raw` on `compress`/`uncompress`, which `Compress::Zlib` reaches
  only through its `Stream` class — and raw deflate plus gzip are exactly what
  an HTTP `Content-Encoding` needs.
- `crc32` and `adler32`, which it does not offer at all.
- `IO::Path` and `IO::Handle` input to the digests, as
  `Digest::SHA256::Native` takes it.
- HMAC's block size. `Digest::HMAC` defaults it to 64 for every hash, which is
  a *non-standard* MAC for SHA-384/512 unless the caller remembers to pass 128.
  Here it defaults to the hash's real block size, which is what RFC 2104 wants;
  pass `64` explicitly and you reproduce `Digest::HMAC` bit for bit.
- `crypt_random(32)` is a real `Int` of any width, and `crypt_random_uniform`
  sizes its draw to the bound — `Crypt::Random` draws four bytes and
  rejection-samples, so an upper bound above `2**32` never terminates there.

## Which implementation answered

Every tag exports a `*-backend()` sub that says so, and it is the first thing
to check when a number looks wrong:

```raku
use Data::Native;
say json-backend();     # 'core' on Raku++; 'JSON::Fast' on Rakudo
```

- **`core`** — the engine's own built-in. Nothing was loaded.
- **`native`** — a `<X>::Native` distribution's compiled extension.
- **anything else** — the name of the module it delegated to.

## What it costs

Nothing, which is the whole design. `use Data::Native` on Raku++ reads no file
and loads no module, so it is not on the program's start-up path at all — where
`use Digest::Native` cost 20.6 ms, `use JSON::Native` 17.0 and
`use Compress::Zlib::Native` 16.0, all of it loading code that then never ran.

The primitives themselves are the engine's, measured on this machine
(2026-09-05, arm64):

| | Raku++ | for comparison |
|---|---:|---|
| CSV parse, 100k rows / 8.5 MB | 39 ms | 926 ms in pure Raku |
| SHA-1, 16 MB | 1,105 MB/s | 868 MB/s from the C extension it replaces |
| MD5, 16 MB | 589 MB/s | 544 MB/s, same |
| deflate, level 6 | 37.1% ratio | libz gets 36.0% |
| inflate | 116 MB/s | libz gets 651 MB/s |

Inflate is the one place the engine is deliberately far behind: it walks a
Huffman table one bit at a time, in the shape whose correctness reads straight
off RFC 1951, because it is the half that meets untrusted input.

## `--exe` and portability

A program using `Data::Native` compiles to a **standalone binary**: there is
nothing to embed and nothing to find at run time, so `--exe --standalone`
builds it and the result needs no disk.

A program that uses a `<X>::Native` distribution *directly* does not — its
compiled extension and its fallbacks' native libraries do not travel, and
`%?RESOURCES` does not exist inside an `--exe` binary. **The rule: a program to
be shipped as a binary uses `Data::Native`, never a `<X>::Native` module.**

## When the module wins instead

The compiler answers the plain case only. In order, it stands aside when:

1. the `use` is **versioned** — `use Data::Native:ver<0.2+>` asked for
   something specific, and the compiler has no version to offer;
2. an explicit **search path** names it (`-I`, `use lib`), which is what makes
   `rakupp test <Dist>` test the distribution rather than the engine;
3. an **installed distribution is newer** than the interface version this
   engine implements — that is how the distributions ship on their own
   schedule, with no engine release;
4. the statement is `need` rather than `use`, which asks for the compunit;
5. the engine has no primitives for that tag.

Otherwise the compiler answers and nothing loads. Installing the distributions
on Raku++ is therefore harmless and does nothing: `use Data::Native` behaves
identically with and without them.

## The `rakupp-` names

Each primitive is also registered under a mechanical `rakupp-<name>` spelling —
`rakupp-to-json`, `rakupp-sha256-hex`, `rakupp-compress`. That is the adoption
hook: it is how the `<X>::Native` distributions find the engine's
implementation, and how another engine could offer the same contract. **The
plain names are not visible without a `use`** — a bare `to-json(1)` is an
undeclared routine, deliberately, and so is a runtime `&::("to-json")`.

A primitive is adopted only if it *answers the contract*, not merely if the
name exists: `rakupp-sha1-hex` returned uppercase hex for several releases,
where every module in that family returns lowercase, and a by-name probe would
have silently changed what a program computed. It answers lowercase now.
