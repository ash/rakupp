# jsonreq — a command-line client for JSON APIs

`curl`+`jq` as one small Raku program: make an HTTP(S) request, parse the JSON
that comes back, optionally pull one value out, print it pretty or minified.
Unlike every other showcase, almost none of it is hand-rolled — the program is
an argument parser and glue around two modules from
[github.com/ash/raku-modules](https://github.com/ash/raku-modules):

- **JSON::Native** does every piece of JSON work: it validates the request
  body before it is sent, parses the response, and prints the result. On
  Raku++ with its compiled extension present it parses natively; everywhere
  else it rides JSON::Fast. The program cannot tell the difference — that is
  the module's contract.
- **HTTP::Simple** carries the request: plain sockets, TLS, redirects,
  retries, basic/bearer auth.

## Usage

```sh
export RAKULIB=$HOME/raku-modules/JSON-Native/lib,$HOME/raku-modules/HTTP-Simple/lib

build/rakupp showcase/jsonreq/jsonreq.raku https://api.github.com/repos/ash/rakupp --query=.stargazers_count
build/rakupp showcase/jsonreq/jsonreq.raku URL                             # GET, pretty-printed
build/rakupp showcase/jsonreq/jsonreq.raku URL --compact                   # minified
build/rakupp showcase/jsonreq/jsonreq.raku URL --query=.users[0].name -r   # one value, bare
build/rakupp showcase/jsonreq/jsonreq.raku POST URL --json='{"a": 1}'      # send JSON
build/rakupp showcase/jsonreq/jsonreq.raku URL --json=@body.json           # body from a file (implies POST)
build/rakupp showcase/jsonreq/jsonreq.raku URL --header='X-Token: t' -v    # -v shows the exchange on stderr
build/rakupp showcase/jsonreq/jsonreq.raku data.json --query=.users[-1]    # a local file — no socket at all
cat data.json | build/rakupp showcase/jsonreq/jsonreq.raku - --compact     # or stdin
```

A target without an `http(s)://` scheme is a **local document** — a path, a
`file://` URL, or `-` for stdin — run through the same query/pretty-print
pipeline. It is presumed JSON, so a broken file dies with the parse error
instead of echoing through (`--raw` prints it untouched); the HTTP-only
options (`--json`, `--header`, `--auth`, `--bearer`, `--insecure`) refuse a
local target.

The `RAKULIB` line is temporary: once the two distributions are on fez,
`zef install JSON::Native HTTP::Simple` replaces it.

| Option | What it does |
|---|---|
| `--query=<path>` / `-q` | extract one value: `.key`, `[n]`, `[-n]` steps, e.g. `.users[0].name` |
| `--compact` / `-c` | minified output instead of pretty-printed |
| `-r` / `--string` | print a Str result bare, like `jq -r` |
| `--sorted` | sort object keys — stable output for diffs |
| `--raw` | print the body untouched, whatever it is |
| `--header='Name: value'` | add a request header (repeatable) |
| `--json=<body>` | request body: JSON text, `@file`, or `-` for stdin; implies POST |
| `--auth=user:pass`, `--bearer=<token>` | HTTP Basic / Bearer authorization |
| `--timeout=<s>`, `--insecure` | exchange timeout; skip TLS certificate verification |
| `-v` / `--verbose` | request line, JSON backend, response status + headers, on stderr |

Behaviour worth knowing: the request body is parsed **before** it is sent, so a
typo dies locally instead of round-tripping as a server-side 400; a non-JSON
response body prints as-is (a JSON tool still shows you the HTML error page);
and an HTTP status ≥ 400 exits 1 after printing the body, like `curl --fail`
except the error body is not thrown away.

## The MAIN story

The whole option table above is one `sub MAIN` signature — the usage text,
type checks, repeatable `--header`, and the `-q`/`--query` aliases all come
from it. `%*SUB-MAIN-OPTS<named-anywhere>` lets the options follow the URL,
which is how a curl-shaped tool reads naturally; supporting that (and
collecting a repeated option into an array, oracle-verified against Rakudo)
went into the Raku++ MAIN dispatcher as part of building this showcase.

## The compare

```sh
RAKUPP=build/rakupp sh showcase/jsonreq/compare.sh
```

starts the [rakus](../rakus) showcase serving [`sample/`](sample) on the
loopback, runs twelve jsonreq commands under Rakudo and under Raku++ —
queries, negative indexes, `null`, a POST refused with a 405, a 404, and two
local-file reads — and diffs STDOUT and exit codes byte-for-byte.

Like every showcase, it compiles: `build/rakupp --exe -o jsonreq
showcase/jsonreq/jsonreq.raku` produces a single binary (interpreter-bundled,
as the program has a `CATCH` block).
