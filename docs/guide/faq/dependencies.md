# FAQ — what does Raku++ depend on?

**Nothing third-party.** The `rakupp` executable links only the libraries
that the operating system already has: the C library, the C++ runtime (on
Linux and Windows this is linked into the binary), threads, the dynamic
loader, and, on macOS, `libiconv`. It does not link OpenSSL, libcurl, libffi,
zlib, ICU, GMP, PCRE, readline, SQLite, libuv or LLVM, and it does not
vendor any of them either.

That does not mean Raku++ cannot do HTTPS or talk to a database. It can, but
the engine does not do it alone: a module drives the system's own library
through NativeCall. The rest of this page lists what is linked, what is
written in-house instead, and what the engine loads only when a program asks
for it.

## Building it

You need two things:

- **CMake 3.16 or newer**
- **a C++17 compiler**: Clang, GCC, MSVC or MinGW-w64

```bash
cmake -S . -B build && cmake --build build
```

There is no bootstrap, so you do not need a Raku to build Raku++. Nothing
else runs during the build either: no Python, no Perl, no code generator.
The Unicode tables are generated C++ that is checked into `src/`, and the
tools baked into the binary (`rakupp install`, `rakupp doc`, `rakupp
upgrade`) are embedded by a CMake script. `git` is optional. When it is
present, the build stamps its `describe` string into `$*RAKU.compiler.build`.

[COMPILERS.md](../COMPILERS.md) explains which compiler to prefer on each
platform and why.

## What the binary links

| Platform | Linked at load time | Inside the binary |
|---|---|---|
| **macOS** | `libSystem` (libc, libm, pthreads, `dlopen`), `libc++`, `libiconv`, all shipped with macOS | — |
| **Linux** (release) | glibc 2.18 or newer: `libc`, `libm`, `libpthread`, `libdl`, the loader | `libstdc++`, `libgcc` |
| **Windows**, MSVC | Windows system DLLs, including `ws2_32` (Winsock) and `bcrypt` (random bytes) | the C runtime (`/MT`), so you do not need the VC++ Redistributable |
| **Windows**, MinGW | the same system DLLs | `libstdc++`, `libgcc`, `winpthread` (`-static`) |
| **BSDs** | the base system's libc and libc++ | — |

Here it is on a Mac, read from a fresh build (version numbers trimmed):

```
$ otool -L build/rakupp
build/rakupp:
	/usr/lib/libiconv.2.dylib
	/usr/lib/libc++.1.dylib
	/usr/lib/libSystem.B.dylib
```

Each release checks the Linux row. `tools/floor-gate.raku` reads the
packaged binaries back and fails the release if they need a glibc newer than
2.18 or a shared `libstdc++`. (Issue #82 is why: v3.26.0 shipped needing
glibc 2.38 even though the docs said "no dependencies".) On Windows, the
release workflow lists the MinGW binary's DLL imports and fails if a MinGW
runtime DLL appears in them.

### What each system library is for

| Library | Used for |
|---|---|
| libc | files, processes, sockets, DNS (`getaddrinfo`), time, locale, `inotify` on Linux |
| pthreads | `start`, `Promise`, `hyper`/`race`, `Supply` schedulers, `Lock` |
| `dlopen` | NativeCall, native extensions, `--jit` kernels |
| libm | `Num` maths |
| iconv | the gb2312, gb18030 and windows-932 encodings, and nothing else. iconv is not available on OpenBSD or Windows, so those three encodings are missing there and everything else works |
| Winsock, `bcrypt` | sockets and the OS random-number generator on Windows |

## Written in-house instead of linked

Usually an interpreter gets these from a library. In Raku++ they are part of
the engine:

| Feature | Instead of |
|---|---|
| `Int` of any size, `Rat`, `FatRat` | GMP / libtommath |
| regexes and grammars, including longest-token matching | PCRE / a regex library |
| the Unicode database: properties, names, normalisation, collation, case mapping, grapheme breaks | ICU |
| zlib, gzip and raw deflate (`Data::Native`) | zlib |
| MD5, SHA-1, SHA-2 and HMAC | OpenSSL's `libcrypto` |
| JSON, which also powers `Rakudo::Internals::JSON`, and CSV | a JSON library |
| random bytes | the OS CSPRNG directly: `getentropy`, `getrandom`, `BCryptGenRandom` |
| TCP and UDP sockets, `IO::Socket::Async` | libuv |
| line editing in the REPL | readline / libedit |

## Loaded only when a program asks

You will not find these in `otool -L` or `ldd`. They are either loaded at
run time or run as separate programs, and only on the code path that needs
them. If one is missing, that one feature fails with an error. Everything
else keeps working.

| Need | What the engine uses | When it is missing |
|---|---|---|
| **NativeCall** (`is native`) | the system **libffi**, loaded with `dlopen`. Set `RAKUPP_FFI` to a path to choose one, or to `0` to turn it off | a built-in fallback handles a narrower set of signatures and throws for the rest. [FFI.md](../FFI.md) has the details |
| **TLS / HTTPS** | not the engine. The `IO::Socket::Async::SSL` module calls the system **OpenSSL** (`libssl`) through NativeCall | the module cannot load. Plain sockets still work |
| **HTTP** | not the engine. Use a module such as `HTTP::Tiny` or Cro. See [http.md](http.md) | — |
| **databases** | not the engine. The `DBIish` drivers call **libsqlite3**, **libpq** or **libmysqlclient** through NativeCall. Their own test suites pass unmodified | `connect` fails with the driver's usual error |
| any other C library | whatever you name in `is native('foo')`. See [native-libraries.md](native-libraries.md) | the usual NativeCall error |
| file watching on macOS | the **CoreServices** framework (FSEvents), loaded with `dlopen`. It ships with macOS | — |
| `rakupp install` / `upgrade` | **curl** and **tar**, run as separate programs | the install fails with the command's own error |
| `--exe`, `--bundle`, `--jit` | a **C++ compiler** on the machine running the command. See [compiling.md](compiling.md) | they fail. Running a script never needs a compiler |

In short:

| Is it built in? | |
|---|---|
| sockets, DNS, JSON, compression, hashing, Unicode, big numbers | **yes**, no `use` needed |
| HTTP | no. Use a module |
| TLS | no. Use a module and the system OpenSSL |
| database connectors | no. Use `DBIish` and the database's own client library |

## Why it is built this way

Raku++ ships as one file. You can copy `bin/rakupp` on its own to another
machine with the same OS and architecture, or `COPY` it into a container, and
it runs. The release CI tests a lone copy with nothing beside it. It does not
depend on any particular version of OpenSSL or libffi.

There is also less to keep up to date. A TLS stack, its certificate store and
its protocol versions are security-critical code that changes often. Raku++
leaves that work to the system's OpenSSL and curl, which the OS updates. If
Raku++ carried its own copy, that copy would age with each Raku++ release.

## Checking it yourself

```bash
otool -L "$(command -v rakupp)"                     # macOS
```

```bash
readelf -d "$(command -v rakupp)" | grep NEEDED     # Linux
```

```bash
objdump -p rakupp.exe | grep -i 'DLL Name'          # Windows (MinGW binutils)
```

To check that nothing from the "Loaded only when a program asks" table is
linked, search the output for `ssl`, `crypto`, `ffi`, `z`, `icu`, `curl` or
`sqlite`. None of them appear.
