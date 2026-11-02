# Ideas — what rakupp could do next

*Started 2026-11-02. Not a plan and not tied to a version: things rakupp could
grow, each checked against the repository on 2026-11-01 as neither built nor
written down in another plan. [V5-IDEAS.md](V5-IDEAS.md) holds what the v5 and
v6 plans left unscheduled; this file holds what nothing here mentioned before.*

Each entry says what the feature is, what in rakupp it would stand on, and,
where one exists, the number that would show it works.

---

## Debugging and reproducing a run

1. **`--record` and `--replay`.** rakupp owns every point where a run's inputs
   come in: the clock, `rand`, files, sockets, `run`, `%*ENV` and NativeCall.
   The sandbox already stops at the same sites for files, processes, sockets
   and NativeCall ([SANDBOX.md](../../guide/SANDBOX.md)). `--record=FILE`
   writes every value that crosses them; `--replay=FILE` feeds them back, so
   the run repeats exactly on another machine after the inputs are gone. A bug
   report becomes one file. With `--trace` and `--repl-after` on the replay, it
   is most of a time-travel debugger, without the DAP debugger that
   [CLI-BORROW-PLAN.md](CLI-BORROW-PLAN.md#deliberate-non-borrows) set aside. A
   first version can record threads only under `RAKUPP_GIL=1`.
   **The number:** a program that reads the time, a file and a socket replays
   to identical output with the file deleted and the network off.

2. **`--seed` fixes the thread schedule too.** `--detect-races` sees only the
   interleaving that happened. A controlled scheduler that lets threads switch
   only at `start`, `await`, `Lock` and Supply emits can run a program under
   many schedules and print the seed of one that fails:
   `rakupp --schedules=500 t/conc.t` → `fails with --seed=8812`. Deadlock
   detection (await cycles, locks taken in opposite orders) fits the same
   machinery; [RACE-DETECTOR-PLAN.md](RACE-DETECTOR-PLAN.md) names it as a
   possible sibling and leaves it.
   **The number:** t/regression/promise-whenever-fires-later-and-in-turn.raku,
   flaky and left open in [TODO-FOLLOW-UPS.md](TODO-FOLLOW-UPS.md), fails
   every time under a printed seed.

3. **Async backtraces that show where the work started.** For
   `sub launch { start { worker() } }` and `await launch()`, rakupp and Rakudo
   2026.09 both report the `die` in `worker`, the block, and the `await`;
   neither says that `launch` was called, or from where. A few frames captured
   when `start` runs would fill that in as a "Started at:" section, as Node's
   async stack traces do.
   **The number:** that program's backtrace names `launch` and the line that
   called it.

4. **Attach a REPL to a running program.** `rakupp --listen=app.sock
   server.raku`, then `rakupp attach app.sock`, opens a REPL inside the live
   process: look at variables, call subs, replace a sub's body as a hot fix.
   `--repl-after` gives a session on a program's state only once the program
   ends. Erlang's remote shell and Clojure's nREPL are the models. Opt-in, and
   only on a local socket guarded by its file permissions. An MCP client could
   attach the same way.

5. **`rakupp explain X::…`.** `rakupp doc X::TypeCheck::Binding` answers
   "nothing in REFERENCE.md or FEATURES.md". Each exception class would get
   what raises it, a minimal program that does, and the usual fix; an uncaught
   error's message could end with the command, as `rustc --explain` does.

6. **An interrupt in the C API.** The ABI has none
   ([EMBEDDING.md](../../guide/EMBEDDING.md#calling-it-from-the-app)). Learn
   Raku added `rk_interrupt(rk)` in its own copy: a flag checked at each
   statement, block entry and Int-kernel loop iteration that raises an error a
   `try` cannot swallow. Four things wait on it:
   - the MCP server cancels a stuck call instead of exiting, which today loses
     the session
     ([MCP.md](../../guide/integrations/MCP.md#lifetime-the-watchdog-and-restarts))
   - Raku.js stops a run; Raku Koans kills its worker with a watchdog
   - Jupyter's "Interrupt kernel"
   - Ctrl-C in the REPL returns to the prompt

   **The number:** an MCP call running `loop {}` times out, and the next call
   still sees the session's subs.

7. **Resource budgets: `--max-steps`, `--max-memory`, `--max-time`.**
   `--sandbox` limits what a program may touch, not how much it may use. A
   step count is deterministic where wall time is not: "exceeded 10⁷ steps" is
   the same verdict on every machine, which is what graders, chat bots and the
   playground need ([V5-IDEAS.md](V5-IDEAS.md#12-random-ideas-beyond-the-current-scope)).
   It shares its check points with the interrupt above.

## Speed: seeing it and getting it

8. **Flame graphs, and a grammar profiler.** `--profile` is flat: one line per
   routine. Recording call paths would let it write speedscope JSON or folded
   stacks for speedscope.app and flamegraph.pl. Separately, `--profile=grammar`
   would give time and backtracks per rule, the first thing the author of a
   slow grammar needs.

9. **"Why is this slow?"** What a user needs is spread over
   `RAKUPP_KERNEL_TRACE`, `RAKUPP_AOT_REPORT`, `--cnp=verbose` and
   `--profile`. `rakupp --why-slow prog.raku` would put it in one report: the
   hot loop, why it got no kernel ("it calls a sub"), and what would change
   that (`my int @a`, hoisting the call). It also answers which of the
   interpreter, `--cnp`, `--jit` and `--exe` to reach for.

10. **C++ inside a sub.** `sub dot(num @a, num @b --> num) is cpp q{ … }`:
    interpreted, the body is compiled and cached by the machinery `--jit`
    already has (`~/.cache/rakupp/jit`, needing a C++ compiler as `--jit`
    does); under `--exe` it is pasted into the generated C++. Native arrays
    arrive as pointers. Perl's Inline::C is the precedent; Rakudo has no fast
    equivalent.
    **The number:** a dot product written this way runs at a C loop's speed
    when called from interpreted Raku.

11. **Cached binaries.** `rakupp --cached app.raku` runs interpreted the first
    time while `--exe` builds in the background; later runs start the binary,
    keyed by hashes of the source and its modules. Only a build that reported
    no fallback is used, so behaviour cannot drift. `go run`'s build cache is
    the model.

12. **Binaries for another platform.** `rakupp --exe app.raku
    --for=linux-x86_64 -o app` on a Mac would compile with `zig c++ -target`
    and link against the runtime from that platform's release archive, fetched
    only behind the flag and checked by SHA-256 as install.sh does. CI already
    cross-compiles rakupp itself from a Mac
    ([COMPILERS.md](../../guide/COMPILERS.md#linux-on-risc-v-built-on-a-mac)).
    The executable slots in
    [V5-IDEAS.md](V5-IDEAS.md#12-random-ideas-beyond-the-current-scope)
    (Lambda, `FROM scratch` images) want Linux binaries.

## Testing and changing code

13. **`rakupp test` with no arguments** runs the project's `t/` in parallel;
    today it prints `usage: rakupp test Module ...`. `rakupp test --doc`
    checks the Pod and README examples that carry `# OUTPUT: «…»` comments,
    the official docs' convention, as
    [tools/doc-examples-diff.raku](../../../tools/doc-examples-diff.raku) does
    for those docs.

14. **Markdown that runs.** `rakupp --md README.md` runs the `raku` code blocks
    in order; `--md --update` writes each block's output back beneath it. This
    repository's docs hold many outputs captured from real runs and kept
    current by hand.

15. **Mutation testing.** `rakupp --mutate t/` changes one thing at a time in
    `lib/` (`<` to `<=`, `+` to `-`, `True` to `False`, a dropped `return`)
    and reports each change the tests still pass with. Coverage says a line
    ran; this says whether the tests would notice it being wrong. It needs
    source → tree → source to round-trip, which `.AST.EVAL` now does
    (586c5160).

16. **Structural search and replace.** `rakupp --rewrite 'PATTERN'
    'REPLACEMENT' lib/`: patterns written in Raku with placeholders, matched on
    the parse tree and written back through `.DEPARSE` and `--fmt`. Uses: 6.c →
    6.e migrations, and a deprecated method renamed across the ecosystem as
    patches authors can take. The language server would get rename and
    find-references from the same machinery;
    [LSP.md](../../guide/integrations/LSP.md#hover-completion-and-go-to-definition)
    lists both as missing, with formatting, which needs only `--fmt` wired in.

17. **`--shuffle-hashes`.** rakupp iterates a hash in insertion order and
    Rakudo does not: inserting `zeta alpha mid beta` gives
    `(zeta alpha mid beta)` here and `(zeta alpha beta mid)` in Rakudo 2026.09.
    Code tested only here can depend on the order unawares and break on
    Rakudo. A flag that randomizes it, pinned by `--seed`, exposes that, as
    Go's randomized map order does. The ecosystem sweep could run with it to
    find suites that pass by order alone.

18. **Grammars that generate.** Walk a grammar's rules the other way and emit
    random strings it accepts, to a depth bound: test data, and a fuzzer for
    any parser written as a grammar. The regex engine is our own, so the rules
    are there to walk.
    **The number:** strings generated from the JSON grammar in
    tools/bench/grammar-json parse with both that grammar and JSON::Fast.

## Modules and the supply chain

19. **Capability lists.** A static pass listing what a module can reach: the
    network, which paths, `run`, NativeCall, `EVAL`.
    `rakupp --capabilities lib/` prints it; raku.online's ecosystem page gets a
    column; raku-eye flags a release that gained one ("Foo 0.4 now calls
    `run`"); `--sandbox` can take its allow list from it.
    **The number:** a list for every distribution in the sweep, and a `run`
    planted in an update is flagged.

20. **Breaking-change checks between releases**, as cargo-semver-checks does:
    compare two versions' exported subs, classes, methods and signatures, and
    flag removals and narrowed signatures. raku-eye runs it beside the
    capability diff.

21. **An SBOM from `--exe`.** `--exe-info` already reads the build manifest in
    a binary. Written as CycloneDX or SPDX, it lists every embedded
    distribution with its version, checksum and META6 licence. The EU Cyber
    Resilience Act asks makers of software products for one.

22. **Reference docs for every distribution**, as docs.rs does for Rust: each
    distribution's Pod beside the signatures of what it exports, taken from
    RakuAST, so a module without Pod still gets a reference page. On
    raku.online next to its ecosystem row; locally `rakupp doc --site lib/`.

23. **Search the ecosystem by syntax.** The sweep already fetches all 2,547
    distributions. Indexed by parse tree, "every call of `.substr-rw`", "every
    `is rw` method" or "every module that uses `nqp::`" is one query. It
    answers "is X used in the ecosystem?", the question every proposal to
    deprecate something runs into, and tells us what to make fast. It is the
    search half of idea 16.

24. **A Unicode security lint.** Bidi control characters in strings and
    comments ("Trojan Source", CVE-2021-42574), and mixed-script or look-alike
    identifiers (Unicode's UTS #39). Raku accepts Unicode identifiers broadly,
    so `$раth` with a Cyrillic `а` and `$path` can sit side by side. The script
    and bidi tables are already in the binary; the confusables list would be
    new data.

## Text, data and numbers

25. **Records in, records out.** `-n` and `-p` over records instead of lines:
    `--in=json|jsonl|csv|tsv` puts each record in `$_` as a Hash or Array, and
    `--out=json` serializes what `-p` prints:

    ```
    rakupp --in=jsonl -ne 'say .<user><name> if .<status> == 500' access.jsonl
    ```

    jq does its arithmetic in doubles; Int and Rat do not round. `from-json`
    already runs in 0.13× Rakudo's time ([V6-PLAN.md](V6-PLAN.md)), and the
    feature belongs with the text pillar ([TEXT-PLAN.md](TEXT-PLAN.md)).
    **The number:** the jq manual's examples as one-liners, and a file of
    prices whose sums jq gets wrong.

26. **A Perl one-liner translator.** `rakupp --from-perl -lane 'print $F[2] if
    /ERR/'` prints the Raku equivalent, and can run it. rakupp already takes
    Perl's `-n -p -a -F -i -0777 -l`, and
    [showcase/perl](../../../showcase/perl/README.md) has a Perl grammar in
    Raku. One-liners only, where the mapping is small and well known.
    **The number:** a share of perl1line.txt translated, each with identical
    output.

27. **Exactness warnings.** `--warn=exactness`, or a lint rule beside
    `exact-power`, names the line where a computation stops being exact: a Rat
    whose denominator overflowed into a Num, a Num that got into a sum of Rats.
    It wants `$*RAT-OVERFLOW` first
    ([TODO-MISCELLANEOUS.md](TODO-MISCELLANEOUS.md)).

28. **Raku as a configuration language**, where Pkl, CUE, Nickel and Jsonnet
    sit: `rakupp --sandbox=language --emit=json|yaml|toml config.raku`
    evaluates a config program and prints its final value. Classes with typed
    attributes and subsets are the schema, so an invalid config fails with a
    line number; the sandbox guarantees no I/O; numbers stay exact.

## Where people meet it

29. **MCP sessions that survive the watchdog**, until the interrupt (idea 6)
    exists: journal the calls that succeeded and replay them on restart,
    skipping the one that hung. Beside it, `rakupp doc` and `--lint --json` as
    MCP tools, so an agent writing Raku can look up builtins offline and check
    code before it runs.

30. **More REPL commands.** The REPL has tab completion, `\t`, `\a` and `\v`.
    Missing: `\s FILE` saves the lines that worked as a program; `\d SYMBOL`
    runs `rakupp doc`; `\time EXPR`; `\e` opens the entry in `$EDITOR`; and
    Ctrl-C back to the prompt once idea 6 lands.

31. **Completions for the user's program.** rakupp prints its own
    (`--completions=zsh`); a program's `MAIN` candidates carry the same
    information: named arguments, enums as value lists, `IO::Path` as file
    completion. `rakupp --completions=zsh app.raku` prints them, and an
    `--exe` binary answers `app --completions=zsh` itself. The playground
    could render `MAIN` as a form.

32. **A Raku explorer on raku.online.** Panes side by side, as on Compiler
    Explorer, updated as you type: the source, `--ast`, `--rakuast`, `--cpp`,
    `--target=js`, and the output. It shows where `--exe` falls back and why,
    the fallback table of [V6-PLAN.md](V6-PLAN.md) made visible per line. To
    check first: whether the WASM build carries the C++ and JS code
    generators.

33. **No-install runs through PyPI and npm.** The `rakulang` wheel bundles
    librakupp but has no command-line entry point. With the `rakupp` binary
    and a console script in it, `uvx --from rakulang rakupp script.raku` works
    wherever Python is, as Ruff and uv ship theirs. On npm, per-platform
    optional packages (esbuild's pattern) give `npx rakupp`. Neither is among
    the channels in
    [V5-IDEAS.md](V5-IDEAS.md#7-where-it-runs-and-from-what).

34. **Error messages in the program's language.** L10N::BG, LV, RU and UK
    install and `.DEPARSE("RU")` renders Russian (aae5d6da), but a program
    written with Russian keywords still gets its errors in English. Under
    `use L10N::RU` or `RAKUPP_LANG=ru` they would follow the program.

---

*Keeping this current: when an idea is picked up, move it to the plan that
builds it and delete it here; when one is decided against, leave it with the
reason.*
