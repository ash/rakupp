# Raku++ showcases

Mid-size programs that each stress a different part of Raku++, chosen so that
together they answer "what can it actually build?"

| Project | Axis it showcases | How you run it |
|---|---|---|
| [**lisp/**](lisp/) | Language power — a grammar + a tree-walking evaluator | interpreter that runs Scheme files or a REPL |
| [**markdown/**](markdown/) | Parsing — a grammar that emits HTML | converter: Markdown in, a styled page out |
| [**pastebin/**](pastebin/) | Deployable — a hand-written HTTP server on raw sockets | native binary you point a browser at |
| [**chat/**](chat/) | Concurrency — many clients, one thread each | TCP chat server you connect to with `nc` |

All paths below are from the repository root, after building `rakupp` (see the
top-level [README](../README.md)). Every program also compiles to a standalone
native binary with `rakupp --exe`.

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

## markdown — the parsing story

A Markdown→HTML converter. Block structure (headings, lists, fenced code,
blockquotes, rules, paragraphs) is recognised line by line; the inline layer
(`**bold**`, `*italic*`, `` `code` ``, `[links](url)`, `![images](url)`) is a
Raku `grammar` with an actions class that emits HTML directly. It reads a file
or stdin and writes a complete, self-contained page.

```sh
build/rakupp showcase/markdown/md2html.raku showcase/markdown/sample.md > out.html
cat notes.md | build/rakupp showcase/markdown/md2html.raku > notes.html
```

The inline grammar uses ordered `||` alternation with a single-character
catch-all, so a stray `*` degrades to literal text instead of failing the
parse — the whole document always converts.

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

## chat — the concurrency story

A multi-client chat server on raw TCP. Every accepted connection gets its own
`start` thread; a shared roster of clients is guarded by a `Lock`, and each
message is broadcast to everyone else. Where the pastebin is one request then
one response, this keeps many long-lived connections interleaving — a blocking
read on one client releases the interpreter lock so the others keep flowing.

```sh
build/rakupp showcase/chat/chat.raku                # listens on 127.0.0.1:6667
# then, in other terminals:
nc 127.0.0.1 6667                                   # first line you send is your nick
```

Send `/who` to list who's online, `/quit` to leave. The first line each client
sends becomes its nick; everything after is broadcast to the room.
