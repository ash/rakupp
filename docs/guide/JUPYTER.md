# The Jupyter kernel — Raku in a notebook

`rakupp --jupyter FILE` runs the engine as a **Jupyter kernel**: cells of Raku
in JupyterLab, Notebook, `jupyter console`, VS Code, or anything else that
speaks the [Jupyter messaging
protocol](https://jupyter-client.readthedocs.io/en/stable/messaging.html)
(version 5.3). A notebook is a REPL with a scrollback you can edit, and that
is exactly what the kernel serves: **one interpreter for the whole notebook**,
so a sub defined in cell 3 is callable in cell 9.

Nothing else has to be installed for the Raku half — no ZeroMQ, no Python
module, no shared library. The `rakupp` binary IS the kernel: it speaks
[ZMTP](https://rfc.zeromq.org/spec/23/) (the wire protocol under ZeroMQ) and
signs its messages with its own HMAC-SHA256, because this project links no
third-party libraries. You still need Jupyter itself — that is the frontend.

## Quick start

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/rakupp --jupyter-install
```

`--jupyter-install` writes a **kernelspec** — the little `kernel.json` that
tells Jupyter how to launch a kernel — into your user Jupyter directory
(`~/Library/Jupyter/kernels/raku` on macOS, `~/.local/share/jupyter/kernels/raku`
elsewhere, `%APPDATA%\jupyter\kernels\raku` on Windows), pointing at the
absolute path of the binary that wrote it. Then:

```sh
jupyter console --kernel raku      # a terminal notebook
jupyter lab                        # and pick "Raku++" in the launcher
```

`--name=NAME` installs under another name (two builds side by side),
`--prefix=DIR` writes to `DIR/share/jupyter/…` instead of the user directory,
and `JUPYTER_DATA_DIR` is honoured when it is set.

To point Jupyter at a build without installing anything, write the kernelspec
by hand — this is all one is:

```json
{
  "argv": ["/absolute/path/to/rakupp", "--jupyter", "{connection_file}"],
  "display_name": "Raku++",
  "language": "raku",
  "interrupt_mode": "message"
}
```

`-M Module` preloads modules into the notebook's session, exactly as it does
for `--mcp` and for a program.

## What a cell does

```raku
my @primes = (1..100).grep(*.is-prime);   #  [2 3 5 7 11 …]
```

A cell is evaluated the way the REPL evaluates a line, and the **value of its
last statement is the cell's result** — whether or not the cell also printed.
`say 42` therefore shows `42` from the printing and then `True`, which is what
`say` returns, and what the `rakupp` prompt shows too. A notebook that hid it
would be teaching a different language.

Everything the cell prints arrives **while it runs**, not at the end: output
crosses on the iopub channel as the engine produces it, so a loop that prints
every second shows its work every second.

Errors are errors, not the end of the notebook: a cell that dies reports the
message and the session carries on with every variable intact.

```raku
my $x = 41;      # cell 1
die "boom";      # cell 2 — an error, and nothing else
$x + 1;          # cell 3 — 42
```

### Rich output

The kernel installs one extra routine, `jupyter-display`, which hands the
frontend something to render instead of text:

```raku
jupyter-display('<b>Bold</b> and <i>italic</i>', 'text/html');
jupyter-display($svg-source, 'image/svg+xml');
jupyter-display('# A heading', 'text/markdown');
```

The MIME type defaults to `text/plain`, and can also be passed as
`:mime<text/html>`. It exists only inside the kernel — a `.raku` file run from
the command line has no frontend to display anything to.

## What it does not do

Two gaps, both deliberate, both visible rather than faked:

- **`get` reads EOF.** A cell's stdin is pinned closed, because a notebook
  frontend may have no way to answer a prompt, and a kernel blocked on one is
  a hung notebook. Read from files and variables, not the keyboard.
- **Interrupt stops the kernel, not the cell.** The engine has no interrupt
  point inside an evaluation, so ■ (interrupt) answers, says so on iopub, and
  *exits*; the frontend restarts the kernel and the session's state is gone.
  That is the same trade the [MCP server](MCP.md) makes with its watchdog: a
  wedged client is worse than a lost session. A cell you may need to stop is
  a cell that should carry its own bound.

Tab completion and `?`-inspection answer politely with nothing, and
`is_complete_request` answers `unknown`, which tells a console frontend to use
its own heuristic for "is this line finished". None of the three can be
answered honestly through today's C ABI; they are candidates for the day it
grows the entry points.

Widgets (`comm` messages) and the debug adapter are not implemented.

## How it works

```
   frontend                          rakupp --jupyter
  ┌──────────┐   shell   (ROUTER) ──▶ execute_request → rk_eval → reply
  │ notebook │   control (ROUTER) ──▶ shutdown / interrupt
  │  client  │   iopub   (PUB)    ◀── stream, execute_result, error, status
  └──────────┘   hb      (REP)    ◀─▶ echo
```

The five ports, the HMAC key and the signature scheme arrive in the
**connection file** Jupyter writes and passes as `{connection_file}`. Every
message is signed with HMAC-SHA256 over its four JSON parts; one that does not
verify is dropped rather than answered.

Engine-side, the kernel is a host of the public C ABI
([EMBEDDING.md](EMBEDDING.md)) — `rk_new`, `rk_eval`, `rk_set_output`,
`rk_register` — exactly as the [MCP server](MCP.md) is. Neither has a private
door into the interpreter, which is why a notebook cell, an agent's tool call
and a `.raku` file all get the same answers.

## The gate

`tools/jupyter-smoke.raku` is a **Jupyter client written in Raku**: it opens
the five sockets itself, performs the ZMTP handshake, implements HMAC-SHA256
from scratch (pinned against the RFC 4231 vectors), and drives the kernel the
way a frontend does — kernel_info, a cell that prints and has a value, the
session carried between cells, a dying cell, `jupyter-display`, a forged
signature that must be ignored, the heartbeat, and shutdown.

```sh
build/rakupp tools/jupyter-smoke.raku
```

Two independent implementations of both halves have to agree for it to pass:
a signature the kernel computes differently is a message the kernel drops.
