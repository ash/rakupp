# Plan: moving rakupp to C++20, later

*A note for later, 2026-11-07. Written during VALUE32-PLAN design C, step 1,
which had to build transparent string lookup by hand because the tree is
C++17. The decision that day: **stay on C++17**. When the move comes, make it in
one step, not as a period of supporting both.*

## Where it stands

- **The code is ready.** Every translation unit in `compile_commands.json`
  (106) passes `-std=c++20 -fsyntax-only` on Apple clang 21 with no errors.
  The warnings are the same as under C++17, minus the "captured structured
  bindings are a C++20 extension" one in Parser.cpp. No `u8"…"` literals
  exist, so `char8_t` breaks nothing. The rewritten `operator==` candidates
  raise no ambiguity.
- **Not checked:** GCC and MSVC in C++20 mode. Neither is on the Mac this was
  surveyed on; the CI legs would be the first to try them.
- **The compilers users have are mostly there already.** `--exe` and the JIT
  compile with the user's own compiler, so the user's compiler sets the floor,
  not only ours:
  - **Linux:** `--exe` already needs GCC 11's libstdc++ (`GLIBCXX_3.4.29`,
    [COMPILERS.md](../../guide/COMPILERS.md#what-runs-where)). GCC 11 takes
    `-std=c++20` and has transparent unordered lookup (P0919, P1690).
  - **musl:** Alpine 3.21, GCC 14.
  - **riscv64:** an Ubuntu 22.04 sysroot (GCC 11/12).
  - **Windows:** `/std:c++20` needs VS 2019 16.11 or newer. MinGW comes from
    MSYS2, whose GCC is current.
  - **macOS:** no floor for the user's compiler is written down anywhere.
    libc++ has had transparent lookup since LLVM 12, about Xcode 13. Write the
    floor into COMPILERS.md as part of the move.
  - **OpenBSD:** the base clang.

## The constraint: one language mode for everything

`--exe` programs and JIT kernels are compiled against the running build's own
`Interpreter.h` and linked against its runtime. On macOS a kernel is built
with `-undefined dynamic_lookup` and binds to the loading executable's symbols.
If a header picked a member's type by language mode (say `classes_` as a
`StrMap` under C++17 and an `unordered_map` with a transparent hash under
C++20), a C++20 build and a C++17-compiled program would disagree on
`Interpreter`'s layout. That corrupts memory with no error.

So the language mode is **one build-time choice** that `--exe` and the JIT
inherit. Never let a header choose it per translation unit. Today C++17 is
hard-coded in five places:

- [CMakeLists.txt:46](../../../CMakeLists.txt) — `CMAKE_CXX_STANDARD 17`.
- [src/main.cpp](../../../src/main.cpp) `nativeCflags` (`-std=c++17`, ~line
  730): `--exe` and its precompiled header.
- [src/main.cpp](../../../src/main.cpp) `compileCmd` (`/std:c++17`, ~line 859):
  `--exe` with MSVC.
- [src/Jit.cpp](../../../src/Jit.cpp) (~lines 749 and 777): the JIT's
  precompiled header and its kernel compile.

The move should replace the four in src/ with a value the build generates
(beside `RAKUPP_VERSION`, in `build/generated`), so they cannot drift from
CMake's choice again.

**Stays as it is:**

- **The Arduino simulator** compiles with `-std=c++11` (main.cpp,
  `--target=arduino --sim`). A sketch includes no rakupp header, and the Due's
  gcc 4.8.3 is C++11 anyway.
- **The embedding headers** in [include/rakupp/](../../../include/rakupp)
  include no `Value.h`. They are what an embedder compiles, in the embedder's
  own mode, so they stay C++17-clean whatever the engine uses. These tools
  hold them to it, and keep `-std=c++17`:
  - tools/bindings-smoke.raku;
  - tools/embed-smoke.raku;
  - tools/grammar-smoke.raku.

**Follow the build's mode:**

- **tools/cpp-build-check.raku** checks what `--cpp` writes, so it should use
  the same `-std` as `--exe`.
- **tools/source-helpers/budget.raku** preprocesses headers with `-std=c++17`.
  Under C++20 the standard headers expand further (below), so look at
  whether its header ceilings count that expansion before switching it.

## The cost: every `--exe` and JIT compile parses more

libc++ pulls in more headers in C++20 mode. Measured 2026-11-07 on Apple clang
21 (arm64), `Interpreter.h` with `--exe`'s flags, three runs each:

| | C++17 | C++20 |
|---|---|---|
| header parse (`-fsyntax-only`) | 0.68 s | 0.82 s (+21%) |
| precompiled header build | 0.75 s | 1.02 s |
| precompiled header size | 42 MB | 50 MB |
| preprocessed lines | 85,447 | 105,206 |

The precompiled header (`~/.cache/rakupp/pch`, and the JIT's own) hides most
of the parse after the first compile. A compile without it
(`RAKUPP_NO_PCH=1`, MSVC) pays the whole difference. GCC's
numbers were not taken. Not tried: libc++'s
`_LIBCPP_REMOVE_TRANSITIVE_INCLUDES`, which might take back some of the
expansion.

## What it would buy

Counted in src/ on 2026-11-07:

- **Transparent unordered lookup.** It is the one feature that was wanted
  for speed, and step 1 of design C covers it without C++20:
  - `ValueHash` and `AttrTable` hash a `string_view` themselves.
  - [src/StrMap.h](../../../src/StrMap.h) keys a `std::unordered_map` on
    views of strings it owns.

  Under C++20, `StrMap` could become a plain `std::unordered_map` with a
  transparent hash. It was measured level as it is.
- **`starts_with` / `ends_with`:** about 455 prefix tests written as
  `compare(0, n, x) == 0` or `rfind(x, 0) == 0`, and 38 uses of hand-rolled
  helpers.
- **`contains`:** about 1,450 `count(k)` calls used as a test.
- **`<bit>`:** IntOps.h has 11 compiler-specific lines, and there are 6 bit
  builtins (`clz`/`ctz`/`popcount`) and 4 `memcpy` type puns that `<bit>` and
  `std::bit_cast` would make portable.
- **`[[likely]]` / `[[unlikely]]`** for the 7 `__builtin_expect` uses.

None of it is speed. Revisit when a feature with a measured payoff needs
C++20, or when the compiler floor goes up for another reason.

## The move, when it comes

1. Generate the mode into `build/generated`, and point the four places in
   src/ above at it.
2. `CMAKE_CXX_STANDARD 20`. Switch tools/cpp-build-check.raku, and look at
   the source budget as described above.
3. Write the floors into
   [COMPILERS.md](../../guide/COMPILERS.md#what-runs-where): GCC 11, VS 2019
   16.11, an Xcode version, and OpenBSD's base clang.
4. Measure `--exe` compile time with and without the precompiled header,
   against the table above.
5. Let every CI leg prove it. These are the first real GCC and MSVC builds in
   C++20 mode:
   - gcc-portability;
   - MSVC;
   - MinGW;
   - musl;
   - riscv64;
   - OpenBSD;
   - i386.

## Rerunning the survey

The C++20 syntax check takes about a minute at `-j10`:
- Take each `.cpp` entry from `build/compile_commands.json`.
- Swap `-std=c++17` for `-std=c++20`.
- Drop `-c`/`-o`/`-MD`/`-MF`/`-MT` and add `-fsyntax-only`.
- Diff the warnings against the same run under C++17.

Run on 2026-11-07, the check turned up two
`-Wreturn-stack-address` warnings that design C step 1 had introduced. The
C++17 build gave them too; nobody had read it. They are fixed in 05ed0372.
