# IDE — plan

*Written 2026-10-07 at `3772da09`, before any code. Goal: editor support for
Raku built into the engine, not into one editor. First a complete Language
Server (`rakupp --lsp`), then a debugger speaking the Debug Adapter Protocol
(`rakupp --dap`). Both are protocols, so VS Code, Emacs (eglot, dape),
Neovim, Zed, Helix and JetBrains (LSP4IJ) get every feature without
per-editor code. The VS Code extension (`editors/vscode`, DeepSoft 1.0.1 on
the Marketplace) stays a thin client.*

**The rule for the debugger, set by the maintainer:** a program run without
the debugger must not get slower. Not "slower within tolerance": the code a
normal run executes stays what it is today. Part D says how that is achieved
and how it is checked.

---

## Where it stands

### The language server

`rakupp --lsp` (src/Lsp.cpp, src/LspIndex.cpp, ~1,700 lines) answers
`initialize`, `didOpen`/`didChange`/`didClose`, `hover`, `completion` and
`definition`; every other request gets -32601.

- **Diagnostics** run the `--lint` pipeline (Lexer → Parser → `lintProgram`
  → `findUndeclaredVars`), synchronously on every `didOpen`/`didChange`, with
  full-document sync and no debounce, version or cancellation. A parse error
  stops the run: one error, nothing after it. A 2,173-line file costs about
  0.11 s per change end to end (`--lint` alone 0.02 s).
- **The index** (LspIndex) is built from the lexer's token stream, not the
  AST, with brace pairs as scopes. It knows declarations (`my`/`has`/
  `constant`/routines/`token`/types/enums/parameters) and `#|`/`#=` comments.
  It is rebuilt from scratch on every hover/completion/definition request, and
  a lexer exception (a half-typed string) leaves it empty. It looks at one
  file at a time: `use` is ignored; methods are matched by name across every
  class.
- **Positions**: `struct Node` carries `line` only (Ast.h:92). Tokens carry
  `line`, `col` and an end offset. `lsp::TextDoc` converts byte offsets to
  UTF-16 columns correctly.
- **Pieces that exist but are not wired**: the lossless highlighter
  (`scanSpans`, src/Highlight.h) for semantic tokens; the formatter
  (`formatSource`, src/Fmt.h) for formatting; `UndeclaredVar::inScope`
  ("did you mean") for a quick fix.
- **Defects**: `lintProgram`, `locateOnLine` and `publish` sit outside any
  `try` (Lsp.cpp:348), so an exception there kills the server;
  `locateOnLine` re-lexes the whole document per finding (findings × size);
  the JSON reader accepts `t`/`f`/`n` by skipping 4–5 characters unchecked
  (Lsp.cpp:173); `rootUri`/`workspaceFolders` are never read, so modules
  resolve against the server's cwd; the parser's module scan touches the disk
  on every keystroke.
- **Not written**: semantic tokens, document and workspace symbols,
  references, rename, signature help, formatting, code actions, folding,
  document highlight, inlay hints, call hierarchy, incremental sync,
  cancellation, `didSave`, watched files.
- **Tests**: t/regression/lsp-reports-undeclared.raku,
  lsp-accepts-stdio.raku, lsp-hover-completion.raku; editors/lsp-demo.sh.

### What a debugger can build on

- **One statement hook already exists.** `Interpreter::exec`
  (InterpreterCore.cpp:1866) runs `if (g_traceStmts) traceStmt(s);` for
  every statement; the paths that never reach `exec` (the tail `return` in
  `callPlainSub`, `callCallableRaw` and the method runner, and
  `execStmtHanding`) carry the same test. `--trace` measured its cost as
  noise (asg −0.2%, loopsum −1.6%, fib +1.5%, interleaved min-of-15;
  CLI-BORROW-PLAN.md). `traceStmt` lives in another translation unit
  (InterpreterModules.cpp:2086).
- **Integer kernels already step aside**: `tryIntKernel` and
  `tryLoopKernel` return early when `g_traceStmts` is set
  (IntKernel.cpp:2646, :2740). `--jit`/`--cnp` are gated by `jit::on()`, a
  startup flag.
- **Every statement has its line**; files come from `Callable::declFile` /
  `curDeclFile()`, which `traceStmt` already resolves.
- **Frames and scopes**: `tctx_.callFrames` (line, code, caller Env per
  call); `btCaptureNow()` (InterpreterBinding.cpp:3876) walks frames with
  each frame's scope; `__sym-stash` enumerates an Env's names and live pad
  slots — `MY::.keys` inside a sub lists its lexicals. All of this is
  thread-local and unsynchronised, so only the thread that owns a frame may
  read it.
- **No interrupt exists**: the Jupyter kernel exits on interrupt and the MCP
  watchdog calls `_Exit`.
- **`--exe` is out of scope**: `$?LINE` is a constant, `callframe` is
  refused, module bodies may be native.

---

## Part L — the language server

Each phase ends with Roast (src/ changed), the `lsp-*` regression tests, and
a check in a real editor (VS Code in an isolated profile, and eglot in batch
Emacs — the recipes are in docs/guide/integrations/).

### L0. Foundations

- [ ] A Raku LSP client for the tests (`t/lib/LspClient.rakumod` or
  `tools/lsp-client.rakumod`): spawn, frame, request/notify, wait for a
  publish. The existing three tests move onto it.
- [ ] Every request and notification handler runs inside one `try`; an
  exception becomes a logged error and, for a request, an error response. A
  test feeds the inputs that throw today (deep nesting, lexer failure).
- [ ] The JSON reader becomes `src/JsonLite.h` (shared with MCP and Jupyter)
  instead of Lsp.cpp's own; `t`/`f`/`n` are checked.
- [ ] One lex per document version: `locateOnLine` uses the cached token
  stream; the index is built once per version and reused by every request.
- [ ] The index survives a lexer failure: tokens up to the failure point are
  kept, so a half-typed string does not blank hover and completion.
- [ ] Diagnostics carry the document version. Before analysing, the server
  drains already-queued messages, so a burst of `didChange` costs one
  analysis; `$/cancelRequest` is honoured for queued requests.
- [ ] `rootUri`/`workspaceFolders` are read; module resolution uses the
  workspace's `lib/` and its META6.json `provides`, not the server's cwd.
  The parser's module lookups are cached per session and invalidated by
  `didChangeWatchedFiles`.
- [ ] **A measurement**: `tools/lsp-bench.raku` replays a typing session
  (open, 200 single-character edits, hovers) against a 2,000-line file and
  prints p50/p95 per request type. Targets: diagnostics p95 under 50 ms,
  hover and completion p95 under 20 ms.

### L1. Structure and navigation within a file

- [ ] `textDocument/documentSymbol`: an outline of packages, classes, roles,
  grammars, routines, methods, `token`/`rule`/`regex`, attributes and
  constants, nested by scope.
- [ ] `textDocument/foldingRange`: brace scopes, Pod blocks, heredocs,
  comment runs.
- [ ] `textDocument/documentHighlight` and `textDocument/references` for
  lexicals (scope-aware: a shadowing `my $x` is another variable) and for
  routines, methods and types within the file.
- [ ] `textDocument/rename` and `prepareRename` on the same resolution,
  refusing names it cannot resolve exactly (a method called through a
  variable of unknown type) rather than guessing.
- [ ] `textDocument/selectionRange` from the brace scopes and tokens.

### L2. Highlighting from the engine

- [ ] `textDocument/semanticTokens/full` (and `/range`) from `scanSpans`,
  mapped from its Pygments classes to LSP token types (keyword, variable,
  function, method, type, string, regexp, comment, number, operator),
  with `declaration` and `readonly` modifiers from the index. It is
  lossless and already tested on Roast, so heredocs, nested quoting and
  regexes come out right where TextMate grammars fail.
- [ ] The extension's TextMate grammar stays as the first paint before the
  server answers, improved only where it is wrong (Pod, heredocs, `#`
  inside strings).

### L3. Across files

- [ ] A workspace index: files under the root are indexed lazily; `use Foo`
  resolves through the workspace `lib/`, META6.json `provides`, and the
  installed repositories (the same lookup `use` does).
- [ ] Definition, hover and completion follow `use` and `is export` into
  those files; installed modules are read-only targets.
- [ ] `workspace/symbol`.
- [ ] References and rename across the workspace for exported names.

### L4. Raku-specific help

- [ ] `textDocument/signatureHelp`: for a `multi`, every candidate, with the
  one the current arguments would select marked when the argument types are
  known literally; for builtins, the signature from REFERENCE.md.
- [ ] Method completion by type where the type is written down
  (`my Foo $x; $x.`, `Foo.new.`, `self.` in a class): the class's methods,
  its parents' and roles', then Any/Mu builtins. Unknown type: today's
  behaviour.
- [ ] Unicode operators: completing `>>` offers `»`, `(elem)` offers `∈`,
  `<=` offers `≤`; hover on either spelling shows the other.
- [ ] Inside a grammar: rule names complete in `<…>`, and go-to-definition
  works on `<rule>`.

### L5. Editing

- [ ] `textDocument/formatting` from `formatSource`; when it refuses (parse
  error, idempotence gate), the response is no edits and a log line. Range
  formatting is not offered.
- [ ] Code actions: "did you mean `$foo`" from `UndeclaredVar::inScope`;
  lint findings that have one obvious fix (unused variable → remove the
  declaration, redundant `return`, self-assignment) gain a fix in
  `LintFinding`, applied by the same code in `--lint --fix` so the editor
  and the CLI cannot differ.
- [ ] `didSave`, and pull diagnostics (`textDocument/diagnostic`) for
  clients that ask — advertised only when implemented (lsp-mode treats any
  `diagnosticProvider` value as support).
- [ ] Parser error recovery, so a file with one error still gets diagnostics,
  outline and hover after it. Large; it touches the parser, so it is last and
  is gated on parse speed (`--stagestats` on the corpus) as well as Roast.

### L6. Responsiveness

- [ ] Incremental sync (`change: 2`); the server patches its copy and
  re-analyses the whole file, which L0's numbers say is fast enough.
- [ ] Analysis on a worker thread, with the reader loop answering
  cancellation and cheap requests while it runs. Only if L0's measurement
  says the single loop misses its targets.

---

## Part D — the debugger

### D-rule: what "not slower" means and how it is checked

1. **No new test on any path a normal run executes.** The debugger reuses
   the existing `g_traceStmts` branch: the flag is renamed to `g_stmtHook`
   (it means "someone wants every statement"), and the out-of-line callee
   decides between `--trace` and the debugger. Kernels and `--jit`/`--cnp`
   step aside on the same flag they already test. The flag is set once,
   before the program starts, and never changes during a run, so no
   statement observes it flipping and no thread races on it.
2. **All debugger code lives in its own translation units**
   (`src/Debugger.cpp` for the engine side, `src/Dap.cpp` for the protocol),
   reached only through the hook's callee and the `--dap` mode.
3. **The deterministic check**: `tools/hook-codegen-check.raku`
   disassembles `Interpreter::exec`, `callPlainSub`, `callCallableRaw`, the
   method runner, `execStmtHanding`, `tryIntKernel` and `tryLoopKernel` from
   a base and a candidate binary, with addresses and symbol names normalised,
   and requires identical instruction sequences. It runs in D0 and again
   whenever a D phase touches a file those functions live in. This is the
   check that cannot be fooled by a noisy machine.
4. **The timing check**: an interleaved A/B of base and candidate, min-of-15,
   on the perf-guard kernels **and** on the same kernels with
   `RAKUPP_NO_KERNELS=1` (half of perf-guard's kernels run inside IntKernel
   and never reach `exec`), plus `tools/dispatch-probe/ab.raku`. Any kernel
   slower beyond the session's measured noise, in the same direction on both
   orders, stops the phase. `perf-guard --check` on a quiet machine before
   the release that ships it.
5. **Anything that would need a new test on a normal-run path** (none is
   planned) is brought to the maintainer with numbers before it is written.

Exceptions follow the same rule: "break on uncaught" uses the top-level
handler, which is already off the normal path; "break on every throw" tests
the flag inside the backtrace capture a thrown `RakuError` already does, and
is measured with a `try { die }` loop kernel before it lands.

### The design

- **Debugging is a mode, chosen at startup.** `rakupp --dap` is the adapter
  (DAP over stdin/stdout, Content-Length framing as in LSP). On `launch` it
  sets `g_stmtHook`, turns off `--jit`/`--cnp`, kernels and `--precomp-*`,
  and runs the program in-process on the interpreter's big-stack thread. A
  program run any other way has no debugger and pays nothing.
- **The hook's callee**, `dbg::onStmt(Stmt*)`: one relaxed atomic load of a
  "stop wanted" word (pause requested, stepping active), then a lookup of
  `s->line` in the current file's breakpoint bitset. Nothing else on the
  running path.
- **Stopping**: the thread that stops parks inside the hook. Every other
  interpreter thread parks at its next statement (they all pass the same
  hook); threads blocked in `await` or I/O stay blocked and are reported as
  running. DAP's `allThreadsStopped` is true.
- **The stopped thread serves its own frames.** Because frames and pads are
  thread-local, the adapter thread never reads them: `stackTrace`, `scopes`,
  `variables` and `evaluate` are posted to the stopped thread's mailbox and
  answered from inside the hook, then it parks again.
- **Stepping** needs no call hooks: step over = stop at the next statement
  whose `callFrames` depth is at most the current one; step in = the next
  statement at any depth; step out = the next statement at a smaller depth.
- **Output**: at startup the adapter keeps a duplicate of fd 1 for the
  protocol and points fds 1 and 2 at pipes, read by a thread and sent as
  `output` events. This catches output from native libraries too, which
  swapping `std::cout`'s buffer (the Jupyter trap) would not. Adapter logs go
  to the C `stderr` duplicate, never `std::cerr`.
- **Variables**: names from the `__sym-stash` walk per scope (lexicals, then
  outer scopes, then the dynamic `$_`, `$/`, `$!`); values as `.raku`, cut at
  a length; Arrays, Hashes, Pairs and objects (attributes, including private
  ones) expand through `variablesReference`.

### D0. The hook, renamed and proven free

- [ ] `g_traceStmts` → `g_stmtHook`; `traceStmt` → a dispatcher calling the
  tracer or `dbg::onStmt` (an empty stub in this phase).
- [ ] `tools/hook-codegen-check.raku`, run on this change: identical
  instruction sequences.
- [ ] The timing A/B from D-rule 4, recorded in this plan.
- [ ] `--trace` output unchanged (its regression tests).

### D1. A program runs under the adapter

- [ ] `--dap` mode: `initialize` (capabilities), `launch` (program, args,
  cwd, env), `configurationDone`, `threads`, `disconnect`/`terminate`;
  `output`, `exited` and `terminated` events; the fd redirection.
- [ ] `tools/dap-client.rakumod`, a DAP client in Raku for the tests, in the
  manner of `tools/jupyter-smoke.raku`.
- [ ] **The differential gate**: the raku-corpus programs and the
  t/regression tests run under `--dap` with no breakpoints produce
  byte-identical output to a plain run. Debug mode switches tiers off; this
  is what proves those switches change nothing a program can see.

### D2. Breakpoints and inspection

- [ ] `setBreakpoints` per file, verified against the lines that have a
  statement (a breakpoint on a blank or continuation line moves to the next
  statement line, and DAP is told); breakpoints set while running take
  effect at the next statement.
- [ ] `stopped` events; `stackTrace` (routine name, file, line per frame,
  from the same walk as `btCaptureNow`); `scopes`; `variables` with
  expansion; `continue`, `pause`.
- [ ] `evaluate` in a frame (hover, watch and the debug console), run on the
  stopped thread in that frame's scope. Side effects are allowed in the
  console and refused for hover (`context: "hover"` evaluates only plain
  variable and attribute reads).
- [ ] Modules: breakpoints in a `use`d module's file bind when it loads.

### D3. Stepping and exceptions

- [ ] `next`, `stepIn`, `stepOut`, by frame depth as above; the tail-return
  sites count as statements.
- [ ] Exception breakpoints: "uncaught" and "every throw" (D-rule above);
  the `stopped` event carries the exception's message and `exceptionInfo`
  its type and backtrace.
- [ ] Threads: `start` blocks and Promise workers appear as DAP threads;
  pausing stops all of them.

### D4. Conveniences

- [ ] Conditional breakpoints, hit counts and logpoints, evaluated in the
  hook only for the breakpoint's own line.
- [ ] Function breakpoints (`sub foo`, `Foo.bar`): the first statement of
  each matching routine body becomes a breakpoint line.
- [ ] `setVariable` for scalars.
- [ ] `runInTerminal`, so a program that reads `$*IN` can run in the
  editor's terminal.

### D5. Clients and documentation

- [ ] The VS Code extension contributes a `raku` debugger type, a default
  launch configuration and a "Debug this file" command, starting
  `rakupp --dap`.
- [ ] docs/guide/integrations/DEBUG.md: VS Code, Emacs with dape (no
  rakupp `.el` package, as decided for the LSP), Neovim with nvim-dap.

### D6. Optional: kernels inside a debug session

- [ ] In a debug session, let a loop or routine kernel run when its line
  range holds no breakpoint and no step or pause is pending; a pause
  requested meanwhile is honoured when the kernel returns. This only changes
  what happens after `tryIntKernel` has already seen the flag set, so it
  costs a normal run nothing. Worth it only if D1's differential runs show
  debug-mode loops too slow to use.

---

## Not in this plan

- A standalone IDE. Comma (IntelliJ-based) showed what that costs to keep
  alive; the protocols reach every editor instead.
- Debugging `--exe` binaries.
- A grammar workbench (live parse tree beside a grammar). It would be a
  VS Code panel or a DAP extension built on this plan, once L and D exist.
- Columns on `struct Node`. Every node would grow, and the interpreter would
  pay for an editor feature; the LSP takes positions from tokens instead.
