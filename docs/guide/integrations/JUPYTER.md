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
cmake --build build -j 4
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
  "interrupt_mode": "message",
  "metadata": {"debugger": false}
}
```

`-M Module` preloads modules into the notebook's session, exactly as it does
for `--mcp` and for a program.

### It replaces another Raku kernel

`raku` is also the name [Jupyter::Kernel](https://github.com/bduggan/raku-jupyter-kernel)
— the kernel for Rakudo — installs under, and Jupyter keeps one kernel per
name. So on a machine that has it, `rakupp --jupyter-install` **replaces**
it: every notebook that asks for the `raku` kernel runs on Raku++ from then
on. The installer says so when it happens, and names the program it replaced:

```
rakupp --jupyter-install: replacing the 'raku' kernel at …/kernels/raku/kernel.json, which launched raku-jupyter-kernel
  (to keep both, install this one under another name instead: rakupp --jupyter-install --name=rakupp)
```

Only `kernel.json` is rewritten. Jupyter::Kernel's other files in that
directory stay, its logo among them, so the launcher shows that logo next to
"Raku++".

To keep both kernels, give this one its own name:

```sh
rakupp --jupyter-install --name=rakupp
jupyter console --kernel rakupp    # Raku++; --kernel raku is still Rakudo
```

JupyterLab then offers "Raku" and "Raku++" side by side. A notebook records
the kernel it was saved with, so a notebook saved under `raku` opens on
whichever kernel holds that name.

To give the name back after a replacement, have Jupyter::Kernel write its
kernelspec again (`--force`, because the file is there):

```sh
raku-jupyter-kernel --generate-config --force
```

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
every second shows its work every second. (The one exception is a cell that
asks for its printed output to be HTML or another type with a
[magic](#cell-magics). That output is held and goes out whole.)

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

### Cell magics

A cell whose first line begins with `#%` or `%%` can say what its value, its
printed output, or the whole cell is. The spelling is
[Jupyter::Kernel](https://github.com/bduggan/raku-jupyter-kernel)'s, so a
notebook written for that kernel (its
[magics.ipynb](https://github.com/bduggan/raku-jupyter-kernel/blob/master/eg/magics.ipynb),
say) runs here unchanged.

```raku
#% html
my @rows = (1..3).map({ "<tr><td>$_</td><td>{ $_ ** 2 }</td></tr>" });
"<table>@rows[]</table>"
```

The cell runs as any cell does, and its value goes to the frontend as HTML
instead of text: a module that returns a chart as an HTML page (a
`<script>` and a `<div>`) renders as the chart.

| First line | What it does |
|---|---|
| `#% html` | the value is HTML |
| `#% markdown`, `#% md` | the value is Markdown |
| `#% latex` | the value is LaTeX |
| `#% latex(ENV)` | the value is LaTeX, wrapped in `\begin{ENV}` … `\end{ENV}` |
| `#% js` | the value is JavaScript, for the frontend to run |
| `#% > html` | what the cell *prints* is HTML (any of the types above); the value stays text |
| `#% latex > html` | both: the value's type, then the printed output's |
| `#% javascript` | the cell is not Raku: it is JavaScript, sent to the frontend as it is |
| `%% bash` | the cell is a bash script; its stdout and stderr are the cell's, as it runs |
| `%% run FILE` | FILE's code, then the cell's, evaluated as one cell |
| `%% always CODE` | CODE runs before every Raku cell from now on (`always prepend CODE`, the same) |
| `%% always append CODE` | … after every Raku cell |
| `%% always show`, `%% always clear` | list them, or forget them all |

`#%` and `%%` are interchangeable: `%% html` is `#% html`. The difference
shows on a line that is not a magic. `#%` begins a Raku comment, so the cell
runs with that line in it. `%%` cannot begin Raku code, so the cell is an
error that lists the magics.

How each one behaves, where it is not obvious:

- **A typed value** goes out as the cell's result, `.gist` as always, under
  the type asked for, with a `[text/html]` stand-in for a client that cannot
  render it (`jupyter-display` does the same). A cell that prints *and* has
  an HTML value shows the printing as text, then the HTML.
- **Typed printed output** (`> html`) is held until the cell ends and goes
  out as one display, because HTML printed a row at a time only renders
  whole. A `jupyter-display` call in the middle of the cell sends what was
  printed before it first, so the order stays the cell's. stderr is never
  held.
- **`#% javascript` and `#% js` differ.** `javascript` alone sends the cell
  itself to the frontend and runs no Raku; `js` sends the Raku value. Whether
  JavaScript runs at all is the frontend's decision. Notebook and JupyterLab
  run it only in an output they trust, and an output the session has just
  produced is one.
- **`%% bash` runs `bash`**, found on `PATH`, and its output streams while it
  runs. A script that exits non-zero, is killed, or cannot start fails the
  cell, so "run all" stops there. Jupyter::Kernel runs the cell with Raku's
  `shell`, which is `/bin/sh`. Any `sh` script is also a bash script, so its
  notebooks still run.
- **`%% run FILE`** reads FILE as the cell would, relative to the session's
  `$*CWD`. Its declarations become the notebook's, which `EVALFILE`'s would
  not.
- **`%% always`** code wraps every cell that is Raku: plain cells, typed
  cells and `run` cells, not `javascript`, `bash` or `always` cells
  (Jupyter::Kernel wraps only the cells that have no magic). Entries
  are joined with `;\n`, so an appended entry becomes the cell's value. An
  entry may be `%% run FILE`, which reads the file again for each cell. A
  cell that only says `%% always` shows the list.

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
([EMBEDDING.md](../EMBEDDING.md)) — `rk_new`, `rk_eval`, `rk_set_output`,
`rk_register` — exactly as the [MCP server](MCP.md) is. Neither has a private
door into the interpreter, which is why a notebook cell, an agent's tool call
and a `.raku` file all get the same answers.

## The gate

`tools/jupyter-smoke.raku` is a **Jupyter client written in Raku**: it opens
the five sockets itself, performs the ZMTP handshake, implements HMAC-SHA256
from scratch (pinned against the RFC 4231 vectors), and drives the kernel the
way a frontend does — kernel_info, a cell that prints and has a value, the
session carried between cells, a dying cell, `jupyter-display`, every cell
magic above, a forged signature that must be ignored, the heartbeat, and
shutdown.

```sh
build/rakupp tools/jupyter-smoke.raku
```

Two independent implementations of both halves have to agree for it to pass:
a signature the kernel computes differently is a message the kernel drops.
