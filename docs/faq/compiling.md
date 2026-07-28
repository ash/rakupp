# FAQ — turning a program into a binary

Raku++ can hand you a standalone executable. There are three ways, and they
trade compile time against speed.

```bash
rakupp --exe    prog.raku -o prog     # native C++ — fastest, may fall back
rakupp --aot    prog.raku -o prog     # parse ahead of time, embed the AST
rakupp --bundle prog.raku -o prog     # embed source + interpreter — always works
```

## Which one do I want?

**`--exe`** compiles your program to C++ and links it against the runtime. It is
the fast one — with `-O`, 4× to 500× over the interpreter depending entirely on
the kernel: string building sits at the low end, tight integer loops at the high
(the prime sieve in `tools/optbench/` is 11.6 s interpreted and 23 ms with `-O`).
Constructs the code generator does not handle yet make it fall back to bundling
for that program, so it always produces a working binary; it just may not be the
fast kind.

**`--aot`** skips parsing at startup by embedding the parsed AST. Same runtime
speed as the interpreter, quicker to start, and it never falls back.

**`--bundle`** embeds your source and the whole interpreter. The most compatible:
whatever runs under `rakupp` runs in the bundle, because it *is* the interpreter.

Add `-O` to `--exe` for the optimising code generator:

```bash
rakupp --exe -O prog.raku -o prog
```

`-O` is what turns integer loops into native arithmetic. On a tight numeric loop
it is worth an order of magnitude; on IO-bound or string-shuffling code it buys
close to nothing.

## `--exe` says it cannot find a compiler

`--exe` and `--bundle` generate C++ and **invoke a C++ compiler at run time** —
the one on your machine, when you build, not when Raku++ was built. If there
isn't one, they fail.

- **macOS** — install the Xcode command-line tools: `xcode-select --install`
- **Debian/Ubuntu** — `apt install g++`
- **Windows** — MSVC (a Visual Studio "Developer Prompt"), or MinGW `g++` on `PATH`
- **Set it explicitly** — `CXX=/path/to/clang++ rakupp --exe prog.raku -o prog`

This catches people who install a *packaged* rakupp (a release tarball, a distro
package) onto a machine with no toolchain: the interpreter works fine and `--exe`
does not. `-e '…'` and running a script never need a compiler.

Which compiler, and why it matters for speed, is in
[COMPILERS.md](../COMPILERS.md).

## The binary is large

The linked runtime dominates. Stripping is the lever that works:

```bash
strip prog        # ~15% smaller
```

`-Os` does *not* help — the size is the runtime, not your code — and it costs
speed. Use `-O` for speed and `strip` for size.

## Does the binary need Raku++ installed?

No. `--exe`, `--aot` and `--bundle` all produce a self-contained executable.
Copy it to a machine with no Raku++ and no Raku and it runs.

It is not cross-platform, though: a macOS build runs on macOS. Build on the
target, or in CI for each target.

## Will my program behave the same compiled?

That is the intent, and it is tested — `tools/run-optbench.raku` checks that the
interpreter, `--exe`, `--exe -O` and Rakudo all produce identical output, and
fails if any row diverges.

It is not a guarantee. The code generator is a second implementation of the
language, and a divergence there is a bug we want reported: run your program both
ways and compare.

```bash
rakupp prog.raku > a.txt
rakupp --exe prog.raku -o prog && ./prog > b.txt
diff a.txt b.txt
```

## Startup time

A compiled binary starts in about 2ms. The interpreter is about the same, since
it does not have a runtime to boot. If you are comparing against another Raku
implementation, startup is one of the larger differences.

---

See also [NATIVE.md](../NATIVE.md) for what compiles natively and
[OPTIMIZATION.md](../OPTIMIZATION.md) for what `-O` actually does.

Back to the [FAQ index](README.md).
