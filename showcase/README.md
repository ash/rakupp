# Raku++ showcases

Mid-size programs that each stress a different part of Raku++, chosen so that
together they answer "what can it actually build?"

| Project | Axis it showcases | How you run it |
|---|---|---|
| [**lisp/**](lisp/) | Language power — a grammar + a tree-walking evaluator | interpreter that runs Scheme files or a REPL |
| [**js/**](js/) | Language power — a full precedence ladder, closures, classes | interpreter that runs JavaScript/TypeScript files or a REPL |
| [**forth/**](forth/) | Language power — a stack machine + word dictionary | interpreter that runs Forth files or a REPL |
| [**perl/**](perl/) | Language power — sigil variables, context, regex | interpreter that runs Perl 5 files or a REPL |
| [**python/**](python/) | Language power — the off-side rule, via an INDENT/DEDENT tokenizer | interpreter that runs Python 3 files or a REPL |
| [**markdown/**](markdown/) | Parsing — a grammar that emits HTML | converter: Markdown in, a styled page out |
| [**json/**](json/) | Parsing — a grammar that round-trips data | parse, pretty-print / minify, and query JSON |
| [**pastebin/**](pastebin/) | Deployable — a hand-written HTTP server on raw sockets | native binary you point a browser at |
| [**rakus/**](rakus/) | Deployable — a general static HTTP file server | point it at a folder, open it in a browser |
| [**chat/**](chat/) | Concurrency — many clients, one thread each | TCP chat server you connect to with `nc` |
| [**kvstore/**](kvstore/) | Protocols — a key-value store with its own text protocol | Redis-style TCP server you drive with `nc` |

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

## js — the industrial-language story

Where lisp shows that a grammar can host *a* language, **js** shows it can host
the language everyone knows. It runs a practical slice of JavaScript — closures,
arrows, classes with `extends`/`super`, template literals, `try`/`catch`,
`for…of`, forty-odd built-in methods — plus enough TypeScript (annotations,
interfaces and generics erased; enums made real) that everyday `.ts` files run
unchanged. The `.js` examples print byte-identical output under `node` and
under `js.raku`, down to `0.1 + 0.2` and JS's truncating `%`.

```sh
build/rakupp showcase/js/js.raku showcase/js/examples/fizzbuzz.js
build/rakupp showcase/js/js.raku showcase/js/examples/bank.ts
build/rakupp showcase/js/js.raku --ast=file.js       # dump the parsed AST
build/rakupp showcase/js/js.raku                     # no file → a REPL
```

The grammar is a nine-level precedence ladder in `rule`s over a token layer;
an actions class folds matches into hash-based AST nodes, and a tree-walking
evaluator with an environment chain does the rest — `return`/`break`/`throw`
travel as typed Raku exceptions. See [`js/README.md`](js/README.md) for the
exact subset.

## forth — the other language model

The same "implement a language" goal as lisp, but the opposite design. Forth has
no parse tree: source is a flat stream of whitespace-separated *words* run against
a shared data stack, and `: name … ;` defines new words from old ones. This one
shows the stack-machine model — a stack, a dictionary, and structured control
words (`if`/`else`/`then`, `begin`/`until`, `do`/`loop`) compiled to a small node
tree and run recursively.

```sh
build/rakupp showcase/forth/forth.raku showcase/forth/examples/demo.fth
build/rakupp showcase/forth/forth.raku               # no file → a REPL
```

```
: square ( n -- n*n )  dup * ;
: fact ( n -- n! )  1 swap 1+ 1 do i * loop ;
5 square .      \ prints 25
10 fact .       \ prints 3628800
```

## perl — parsing the ancestor

Raku grew out of Perl, so the sharpest language-power demo is Raku++ parsing its
own predecessor. A grammar reads a practical slice of Perl 5 — sigil variables
(`$`/`@`/`%`), scalar-vs-list context, statement modifiers, string interpolation
— and a tree-walking evaluator runs it. Regex, Perl's signature feature, is a
small backtracking engine written inside the interpreter, since rakupp can't
compile a regex from a runtime string.

```sh
build/rakupp showcase/perl/perl.raku showcase/perl/examples/wordfreq.pl
build/rakupp showcase/perl/perl.raku showcase/perl/examples/regex.pl
build/rakupp showcase/perl/perl.raku               # no file → a REPL
```

```perl
my %freq;
$freq{$_}++ for split /\s+/, $text;
for my $w (sort { $freq{$b} <=> $freq{$a} or $a cmp $b } keys %freq) {
    printf "%-6s %d\n", $w, $freq{$w};
}
```

All six example programs produce byte-identical output under the system `perl`
and under `perl.raku`. References and nested data structures, `tr///`, packages
and file I/O are out of scope — see [perl/README.md](perl/) for the exact
boundary.

## python — the off-side rule

Python's blocks are delimited by indentation, which a PEG grammar can't track on
its own. So, like CPython, a tokenizer pass converts indentation into explicit
`INDENT`/`DEDENT` markers first, and an ordinary grammar parses the marker
stream — its block rule is just `suite: NEWLINE INDENT stmt+ DEDENT`, with no
indentation logic. Run any example with `--tokens` to watch a program turn into
that stream.

```sh
build/rakupp showcase/python/python.raku showcase/python/examples/fizzbuzz.py
build/rakupp showcase/python/python.raku --tokens=file.py   # show INDENT/DEDENT
build/rakupp showcase/python/python.raku                    # no file → a REPL
```

```python
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

print([fib(i) for i in range(15)])
```

All five example programs produce byte-identical output under CPython 3 and
under `python.raku` — arbitrary-precision ints, `0.1 + 0.2` printing
`0.30000000000000004`, comprehensions, closures, and `lambda`-key sorting
included. Classes, imports, exceptions and generators are out of scope; see
[python/README.md](python/) for the exact boundary and a walk-through of the
indentation tokenizer.

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

## json — the round-trip story

A JSON parser *and* formatter: a grammar reads JSON into native Raku values, and
a serializer writes them back — pretty-printed or minified — with an optional
`jq`-style path to pull one value out.

```sh
build/rakupp showcase/json/json.raku showcase/json/sample.json           # pretty-print
build/rakupp showcase/json/json.raku --compact showcase/json/sample.json # minify
build/rakupp showcase/json/json.raku --query='.users[0].name' showcase/json/sample.json
```

Full string escapes (`\n`, `\uXXXX`, …) decode and re-encode; object keys are
emitted sorted, so the output is stable for diffs.

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

## rakus — the general-server story

Where the pastebin is one app with a fixed route table, **rakus** is a reusable
static HTTP file server — the Raku answer to `python3 -m http.server`, on nothing
but `IO::Socket::INET`. Point it at a folder and it serves the files with the
right `Content-Type` (text and binary), `index.html` or an auto directory listing,
`HEAD`, `301`/`403`/`404`, one thread per connection.

```sh
build/rakupp showcase/rakus/rakus.raku              # serves ./public on :8080
build/rakupp showcase/rakus/rakus.raku 9000 ~/site  # choose the port and root
```

Open <http://127.0.0.1:8080/> — the bundled `public/` folder has a styled page,
an SVG logo, and a `files/` directory with no index so you can see the listing.

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

## kvstore — the protocol story

A key-value store with a Redis-flavoured text protocol, over raw TCP. Like the
chat server it is concurrent — one `start` thread per connection — but instead of
broadcasting, the connections share one mutable store guarded by a `Lock`, and
each command line gets a reply. It's the "network protocol" showcase: no HTTP,
just a line protocol you can type by hand.

```sh
build/rakupp showcase/kvstore/kvstore.raku          # listens on 127.0.0.1:6380
# then, in another terminal:
nc 127.0.0.1 6380
```

```
SET name ada             -> OK
GET name                 -> ada
SET greeting "hi there"  -> OK      (quotes keep the spaces)
INCR hits                -> 1
APPEND name " lovelace"  -> 12
KEYS                     -> greeting hits name
DEL name                 -> 1
HELP                     -> the command list
```

Commands: `SET GET DEL EXISTS INCR DECR APPEND KEYS DBSIZE FLUSHALL PING QUIT`.

## In the browser — [`web/`](web/)

The pure showcases also run **client-side in the browser**, on rakupp compiled to
WebAssembly (see [`rakujs/`](../rakujs/)). [`web/`](web/) has three little apps that
reuse the showcase code directly: a **live Markdown editor**, a **JSON
beautifier/minifier**, and a **regex tester + grammar explorer** with match
highlighting and parse-tree output. The server showcases can't run there — the
sandbox has no sockets.

```sh
showcase/web/bundle.sh         # then open showcase/web/index.html — no server
```

`bundle.sh` builds Raku.js if needed and embeds it (WebAssembly as base64) so the
apps open straight from disk, `file://` — see [`web/README.md`](web/README.md).
