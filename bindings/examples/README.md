# The shared examples — the Raku half

Every binding ships the same two example programs. This directory holds the
part they *share*: the Raku each one runs, and the output each one must
produce. The programs themselves live with their language, in
`bindings/<lang>/examples/`, because that is where each toolchain expects to
find them — `cargo run --example` and `go run ./examples/…` require it, and
the other four follow the same shape so all six read alike.

| example | shows | the Raku it runs |
|---|---|---|
| **calc** | running Raku from a host: evaluate source, call subs with host values, read results back, catch a die | [calc.raku](calc.raku) |
| **shopping** | parsing with a Raku grammar: compile, walk a match lazily, read what actions computed, diagnose a failure | [shopping.raku](shopping.raku) |

Neither example needs the other. If you are new to the bindings, read `calc`
first — it is the general story, and grammars are a specialisation of it.

## Where each language's program is

| language | program | guide |
|---|---|---|
| Python | [../python/examples/](../python/examples/) | [../python/README.md](../python/README.md) |
| JS (Bun) | [../js/examples/](../js/examples/) | [../js/README.md](../js/README.md) |
| Go | [../go/examples/](../go/examples/) | [../go/README.md](../go/README.md) |
| Rust | [../rust/examples/](../rust/examples/) | [../rust/README.md](../rust/README.md) |
| C++ | [../cpp/examples/](../cpp/examples/) | [../cpp/README.md](../cpp/README.md) |
| Wolfram Language | [../wolfram/examples/](../wolfram/examples/) | [../wolfram/README.md](../wolfram/README.md) |

## Running them

Once, from the repo root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
cmake --build build -j
```

Then, still from the repo root, with `<example>` being `calc` or `shopping`
(`.so` instead of `.dylib` on Linux):

```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/python/examples/<example>.py
```
```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib bun bindings/js/examples/<example>.mjs
```
```bash
cd bindings/go && CGO_LDFLAGS="-L$PWD/../../build -Wl,-rpath,$PWD/../../build" go run ./examples/<example>
```
```bash
RAKUPP_LIB_DIR=$PWD/build cargo run --manifest-path bindings/rust/Cargo.toml --example <example>
```
```bash
c++ -std=c++17 -Iinclude bindings/cpp/examples/<example>.cpp build/librakupp.dylib -Wl,-rpath,$PWD/build -o ex && ./ex
```
```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib wolframscript -file bindings/wolfram/examples/<example>.wls
```

Or run all twelve at once, and have the outputs checked for you:

```bash
build/rakupp tools/bindings-smoke.raku
```

## What they print

`calc` prints the same seven lines in every language — six languages
agreeing byte for byte is the claim it exists to make:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` prints six lines, five of them identical everywhere and one that
deliberately is not — the line where each host dumps the whole match as its
*own* native data (`dict` in Python, `Object` in JS, `map[string]interface{}`
in Go, and so on).

Every one of these outputs is recorded in [expected/](expected): `calc.txt`
is shared by all six hosts, and `shopping.<host>.txt` pins each language's
own. Those files are the expectation `tools/bindings-smoke.raku` checks;
after an intended change, re-record with
`build/rakupp tools/bindings-smoke.raku --record`.

## Adding an example

The gate discovers examples from the `.raku` files in *this* directory, so
nothing needs editing to add one:

1. Write `<name>.raku` here.
2. Write the six programs, one per language, each in its own
   `bindings/<lang>/examples/`: `<name>.py`, `<name>.mjs`, `<name>.cpp`,
   `<name>/main.go` (Go wants a directory per `main`), `<name>.rs`, and
   `<name>.wls`.
3. Run `build/rakupp tools/bindings-smoke.raku --record`. If all six hosts
   agree it writes one `expected/<name>.txt`; if they differ it writes one
   file per host and says so.

[../README.md](../README.md) compares all six languages side by side.
