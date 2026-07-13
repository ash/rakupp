# Raku++ showcases

Two mid-size programs that each stress a different part of Raku++, chosen so
that together they answer "what can it actually build?"

| Project | Axis it showcases | How you run it |
|---|---|---|
| [**lisp/**](lisp/) | Language power — a real grammar + a tree-walking evaluator | interpreter that runs Scheme files or a REPL |
| [**pastebin/**](pastebin/) | Deployable — a hand-written HTTP server on raw sockets | native binary you point a browser at |

All paths below are from the repository root, after building `rakupp` (see the
top-level [README](../README.md)).

## lisp — the language-power story

A small but real Scheme: lexical closures, `define`/`lambda`/`let`/`cond`,
`set!`, quote/quasiquote/unquote-splicing, tail-recursive evaluation, and a
numeric tower that rides directly on Raku's own — so exact `Rat`s and unbounded
`Int`s come for free (`(fact 100)` prints all 158 digits).

```sh
build/rakupp showcase/lisp/lisp.raku showcase/lisp/examples/fact.scm
build/rakupp showcase/lisp/lisp.raku showcase/lisp/examples/closures.scm
build/rakupp showcase/lisp/lisp.raku                 # no file → a REPL
```

The reader is a Raku `grammar` plus an actions class — source text turns
straight into Raku data structures, which is exactly the job a language
implementation exists to do.

## pastebin — the deployable story

A pastebin served over HTTP/1.1, written directly on `IO::Socket::INET` with no
framework: it parses requests by hand, keeps pastes in memory, and serves an
HTML UI plus a raw-text endpoint. Compile it and you have a single native
binary you can host.

```sh
build/rakupp showcase/pastebin/pastebin.raku        # open http://127.0.0.1:8080
PORT=9000 build/rakupp --exe -o pastebin showcase/pastebin/pastebin.raku && ./pastebin
```

| Route | What it does |
|---|---|
| `GET /` | create form + recent pastes |
| `POST /paste` | store `content=…` → 303 redirect to the new paste |
| `GET /p/<id>` | view one paste (HTML) |
| `GET /raw/<id>` | view one paste (text/plain) |
