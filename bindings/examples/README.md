# The shared examples — two programs, five languages each

Every binding ships the same two example programs. They are the fastest way
to check a binding works on your machine, they are what
`tools/bindings-smoke.raku` runs, and each language's guide walks through its
version line by line.

| example | shows | the Raku it runs |
|---|---|---|
| **calc** | running Raku from a host: evaluate source, call subs with host values, read results back, catch a die | [calc.raku](calc.raku) |
| **shopping** | parsing with a Raku grammar: compile, walk a match lazily, read what actions computed, diagnose a failure | [shopping.raku](shopping.raku) |

Neither example needs the other. If you are new to the bindings, read `calc`
first — it is the general story, and grammars are a specialisation of it.

## Prerequisite

Once, from the repo root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
cmake --build build -j
```

## Running them

From the repo root, with `<example>` being `calc` or `shopping` (`.so`
instead of `.dylib` on Linux):

| language | files | run |
|---|---|---|
| Python | [calc.py](calc.py), [shopping.py](shopping.py) | `RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/examples/<example>.py` |
| JS (Bun) | [calc.mjs](calc.mjs), [shopping.mjs](shopping.mjs) | `RAKUPP_LIB=$PWD/build/librakupp.dylib bun bindings/examples/<example>.mjs` |
| Go | [../go/examples/](../go/examples/) | `cd bindings/go && CGO_LDFLAGS="-L$PWD/../../build -Wl,-rpath,$PWD/../../build" go run ./examples/<example>` |
| Rust | [../rust/examples/](../rust/examples/) | `RAKUPP_LIB_DIR=$PWD/build cargo run --manifest-path bindings/rust/Cargo.toml --example <example>` |
| C++ | [calc.cpp](calc.cpp), [shopping.cpp](shopping.cpp) | `c++ -std=c++17 -Iinclude bindings/examples/<example>.cpp build/librakupp.dylib -Wl,-rpath,$PWD/build -o ex && ./ex` |

Or run all ten at once, and have the outputs checked for you:

```bash
build/rakupp tools/bindings-smoke.raku
```

## What they print

`calc` prints the same seven lines in every language — five languages
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

Every one of these outputs is recorded in [expected/](expected):
`calc.txt` is shared by all five hosts, and `shopping.<host>.txt` pins each
language's own. Those files are the expectation
`tools/bindings-smoke.raku` checks; after an intended change, re-record with
`build/rakupp tools/bindings-smoke.raku --record`.

## Adding an example

The gate discovers examples from the `.raku` files in this directory, so
nothing needs editing to add one:

1. Write `<name>.raku`.
2. Write `<name>.py`, `<name>.mjs`, `<name>.cpp` here, plus
   `../go/examples/<name>/main.go` and `../rust/examples/<name>.rs`.
3. Run `build/rakupp tools/bindings-smoke.raku --record`. If all five hosts
   agree it writes one `expected/<name>.txt`; if they differ it writes one
   file per host and says so.

[../README.md](../README.md) compares all five languages side by side.
