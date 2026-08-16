# The shared example — one grammar, every language

[shopping.raku](shopping.raku) parses a free-form shopping list of `milk=2`
pairs — its rules are `rule`s, so the layout and the spacing around the `=`
are up to you — and its actions class sums the quantities inside the parse.
The same program exists in every binding's language, prints the same thing,
and is the fastest way to check a binding works on your machine.

Prerequisite (once, from the repo root):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
cmake --build build -j
```

Then, from the repo root (`.so` instead of `.dylib` on Linux):

| language | file | run |
|---|---|---|
| Python | [shopping.py](shopping.py) | `RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/examples/shopping.py` |
| JS (Bun) | [shopping.mjs](shopping.mjs) | `RAKUPP_LIB=$PWD/build/librakupp.dylib bun bindings/examples/shopping.mjs` |
| Go | [../go/examples/shopping](../go/examples/shopping/main.go) | `cd bindings/go && CGO_LDFLAGS="-L$PWD/../../build -Wl,-rpath,$PWD/../../build" go run ./examples/shopping` |
| Rust | [../rust/examples/shopping.rs](../rust/examples/shopping.rs) | `RAKUPP_LIB_DIR=$PWD/build cargo run --manifest-path bindings/rust/Cargo.toml --example shopping` |
| C++ | [shopping.cpp](shopping.cpp) | `c++ -std=c++17 -Iinclude bindings/examples/shopping.cpp build/librakupp.dylib -Wl,-rpath,$PWD/build -o shopping && ./shopping` |

Each language's guide (`bindings/<lang>/README.md`) walks through its
version line by line and lists the failure modes; [../README.md](../README.md)
compares all five.
