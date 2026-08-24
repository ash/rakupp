# The MCP server — giving an AI agent this interpreter

`rakupp --mcp` serves the engine over the
[Model Context Protocol](https://modelcontextprotocol.io): JSON-RPC 2.0, one
message per line, on the server's stdio. An MCP client — Claude Code, Claude
Desktop, Cursor, and their kind — launches the process, discovers its tools,
and calls them mid-conversation. This server offers two:

- **`raku`** — evaluate Raku in **one persistent session**. State
  survives across calls exactly as in the REPL, because it *is* the REPL's
  evaluation path reached through the embedding ABI: the agent defines a sub
  in one call and uses it ten calls later. The arithmetic is the engine's —
  exact Rats (`0.1 + 0.2 == 0.3` is `True`) and integers that never
  overflow — which makes the tool useful to agents on tasks that have
  nothing to do with Raku: it is a calculator that does not round.
- **`raku-parse`** — compile a grammar from source text and parse with it.
  A match comes back as a JSON tree; a failed parse comes back *diagnosed* —
  line, column, and the deepest rule the engine reached — which is exactly
  what an agent needs to fix its grammar and try again.

Everything engine-side goes through the public C ABI
([EMBEDDING.md](EMBEDDING.md)); the grammar service is the same
`rk_grammar_shim()` every language binding loads. The server is another host
of that ABI, not a private door into the interpreter — what an agent gets is
byte-for-byte what a binding (or plain `rakupp`) would get.

## Quick start

No shared library is needed — the CLI binary itself serves:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Register it with an MCP client. Claude Code:

```sh
claude mcp add raku -- /absolute/path/to/build/rakupp --mcp
```

or per-project, in a `.mcp.json`:

```json
{
  "mcpServers": {
    "raku": {
      "command": "/absolute/path/to/build/rakupp",
      "args": ["--mcp"]
    }
  }
}
```

Claude Desktop takes the same `command`/`args` shape in its
`claude_desktop_config.json` under `mcpServers`.

To see it speak without any client, pipe requests in by hand — one JSON
object per line:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"you","version":"0"}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"raku","arguments":{"code":"(1..10).grep(*.is-prime).sum"}}}' \
  | build/rakupp --mcp
```

## raku

One argument, `code`. The tool is named after the language — no `_eval`
suffix — because that is how it is asked for ("run some raku") and because
`raku(code)` is the CLI's `raku -e` wearing a protocol. The reply is what the code **printed**; when it
printed nothing, the value of its last statement is shown instead, as
`=> value` (the `.gist`, the REPL's convention). Anything the code wrote to
stderr arrives under a `STDERR:` heading. A `die` crosses as a tool error
carrying the message — and the session survives it, so an agent can probe
freely.

The session is the whole point. `my $x = 41` in one call, `$x + 1` in the
next, is `42`: agents build up state — load a module, define helpers, then
work — the way a person uses a REPL. `-M` on the server command line
preloads modules into that session (`rakupp --mcp -M JSON::Fast`), and the
session resolves `use` from the same places the CLI does; point `RAKULIB` at
extra directories rather than `-I`, which `--mcp` refuses (the flag belongs
to program runs).

Two honest caveats. `exit` in evaluated code does NOT end the server — an
embedded evaluation refuses to end its host process — so it comes back as an
error naming the code (`exit(7) in evaluated code — …`) and the session
continues. And the interpreter's stdin is pinned to EOF (`rk_set_input`), so
`get`/`lines`/`$*IN` see an empty input rather than eating protocol bytes;
feed data through the code itself.

## raku-parse

Kebab-case, as the language itself spells identifiers.

| argument | meaning |
|---|---|
| `grammar` | Raku source declaring the grammar — the text a `.raku` file would hold |
| `text` | the input to parse |
| `name` | the grammar's name in the source; may be omitted only when the grammar declaration is the source's **last statement** |
| `actions` | an actions class named in the same source (needs `name`); a fresh instance per parse, and the top-level `.made` comes back as `made` |
| `rule` | parse a fragment with this one rule instead of anchoring `TOP` to the whole input |

The answer is JSON. A match:

```json
{"matched":true,"tree":{"a":"2","b":"40"},"made":42}
```

with the bindings' tree shape ([bindings/README.md](../../bindings/README.md)
section 4): a node with no captures is its matched text, named captures are
keys, positional captures are `"0"`, `"1"`, …, and a quantified capture is
an array. Anything the actions printed rides along as `output`. A non-match:

```json
{"matched":false,"diagnosis":{"pos":6,"line":1,"column":7,"rule":"num"}}
```

— rule-grained, like the bindings' ParseError: the position is where the
deepest failing *rule* started.

Compilation is cached by `(grammar, name, actions)`, so re-parsing with the
same grammar costs one lookup. **Pass `name` whenever iterating on a
grammar**: an edited named grammar recompiles cleanly into a fresh handle,
while an unnamed grammar refuses recompilation under the same name (the
shim's protection against silently rebinding earlier handles — the refusal
message says exactly this).

## Lifetime, the watchdog, and restarts

One interpreter per process is the ABI's rule, and it maps perfectly here:
one server process per client session, one session per conversation. The
server exits when its client closes stdin — that is how MCP clients end a
server, and it exits `0`.

A call that cannot finish is answered anyway. `rk_eval` cannot be
interrupted mid-evaluation (an `rk_interrupt` is future ABI work), so a
watchdog answers the in-flight request with a tool error after `--timeout`
seconds — default 120, `--timeout=0` disables — and then **exits the
process**. The client restarts the server on its next call; session state is
lost, and the error text says so. A wedged agent is the alternative, and it
is worse.

## Security

The `raku` tool executes arbitrary Raku **with your privileges** — file system,
network, `run`. Registering this server grants an agent exactly the trust
that giving it a shell does; on a machine where the agent already runs
commands (Claude Code's default), that is no new exposure, but it is worth
saying out loud. There is no sandbox in this server. A sandboxed, remotely
hosted variant is a separate piece of work (the Raku.js WebAssembly build is
the natural cage for it), deliberately out of scope here.

## Testing

```sh
build/rakupp tools/mcp-smoke.raku
```

drives a real server over stdio exactly as a client does and pins the
answers: the handshake, both tools, session persistence, output capture,
25!, exact Rats, a die that the session survives, the tree and the
diagnosis, both protocol error codes, and the watchdog's answer-then-exit
contract. It runs in CI on every push, beside the embed, grammar, and
bindings gates.

## Design notes

The server lives in [src/McpServer.cpp](../../src/McpServer.cpp) behind the
`--mcp` flag, speaks the protocol floor every MCP revision shares
(`initialize`, `tools/list`, `tools/call`) and echoes the client's protocol
revision back. Frames are newline-delimited, so the protocol reads fd 0
directly — `rk_set_input` redirects `std::cin` by design, and the server
must not starve with it. It creates the interpreter lazily at the first tool
call (`initialize` answers instantly), with `own_stack` on, so deep
recursion in evaluated code meets the engine's own guard exactly as it does
under the CLI. The JSON layer is ~300 lines of this file rather than a
dependency, for the same reason the engine has no other dependencies.
