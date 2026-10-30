# FAQ — "Unknown VM", "Unexpected backend name: cpp", and RAKUPP_VM_NAME

A few modules check which virtual machine they run on, and stop if it is not
one they know. On Raku++ that check sees `cpp`, so they stop with messages
like these:

```
Unknown VM; don't know how to build
Unexpected backend name: cpp
```

This page explains what the messages mean, the environment variable that gets
past them, and when that variable is the right thing to use. The other places
the two engines differ are in [differences.md](differences.md), and installing
modules in general is in [modules.md](modules.md).

Checked 2026-10-18 with Raku++ 5.4.1 (main) and Rakudo v2026.09; the
installer's second run as `moar` 2026-10-30.

## `rakupp install LibraryMake` says it is "testing it again as `moar`"

```console
$ rakupp install LibraryMake
…
testing LibraryMake: 1 file
LibraryMake stops on $*VM.name, which is `cpp` here: testing it again as `moar`
installed LibraryMake:ver<1.0.5>:auth<zef:jjmerelo>
```

LibraryMake's own test calls its `get-vars`, which knows `moar` and `jvm`
and dies *Unknown VM; don't know how to build* on anything else. When a
distribution's test suite stops with that message, or with *Unexpected
backend name*, the installer runs the suite once more with `$*VM.name`
answering `moar` (the variable below, set for that run only), says so, and
installs if it passes. Before 2026-10-30 it refused, and LibraryMake needed
`RAKUPP_VM_NAME=moar rakupp install LibraryMake`; that still works.

LibraryMake is a build
helper: 29 distributions in the ecosystem depend on it, most of them to compile
their C code when they are installed. Among them are LibXML, Font::FreeType,
Digest::SHA1::Native, Math::DistanceFunctions::Native and
Math::SparseMatrix::Native. Once
LibraryMake is in the store, those install with a plain `rakupp install`,
because the installer already gives their build step the name it expects
([below](#why-is-moar-not-the-default)).

## Why does `$*VM.name` say `cpp`?

It names the backend that runs the program, as it does in Rakudo:

```raku
say $*VM.name;
say $*RAKU.VMnames;
```

```
Raku++   cpp      (cpp js)
Rakudo   moar     (moar jvm js)
```

`cpp` is the interpreter and `--exe`; a program compiled with `--target=js`
answers `js`. Rakudo answers `moar` on MoarVM and `jvm` on the JVM.

A module that checks for `moar` usually does it to read build settings out of
`$*VM.config`: the C compiler, the object-file suffix, how a shared library is
named. Raku++ fills `$*VM.config` with this machine's real toolchain, so the
code in the `moar` branch works here, and the name is the only thing in the way:

```raku
say $*VM.config<cc obj dll ldshared>;
```

```
Raku++   (cc .o lib%s.dylib -dynamiclib)
Rakudo   (clang .o lib%s.dylib -dynamiclib)
```

(macOS; on Linux the library is `lib%s.so` and the flag `-shared`.)

## What exactly does `RAKUPP_VM_NAME` change?

Three answers, and nothing else:

```console
$ RAKUPP_VM_NAME=moar rakupp -e 'say $*VM.name; say $*RAKU.compiler.backend; say $*RAKU.VMnames'
moar
moar
(cpp js moar)
```

- `$*VM.name` answers the value you gave.
- `$*RAKU.compiler.backend` answers it too. In Rakudo the two are the same
  string, and a module that reads one of them must not find the other one
  saying something different.
- The value is added to `$*RAKU.VMnames`, so `$*VM.name eq any($*RAKU.VMnames)`
  still holds.

`$*VM.config` stays as it was, and so does the engine. The variable adds no
`nqp::` operations and no MoarVM internals, so a module that really needs
MoarVM gets further and then fails anyway. An empty value is ignored, and
the name stays `cpp`. Rakudo has no such variable: it answers `moar` with
the variable set or not.

## Why is `moar` not the default?

Because it is not true, and other programs ask the same question for other
reasons. A program that sees `moar` may take a path written for MoarVM, use
`nqp::` operations that only exist there, or report the wrong backend in a
bug report. The default is the honest answer, and the variable is for when
you have looked at a module and decided its `moar` branch is the right one
here.

There is one place the installer answers `moar` on its own: the **build step**
of a distribution, a `Build.rakumod` or a `builder` named in `META6.json`. The
ecosystem's build recipes read their toolchain behind a `moar` check and have
no other branch, and that step is the installer's own work. A test suite that
stops on the name gets one more run with it, as above: LibraryMake has no
build step but tests its own recipe. Nothing else sees the name: not your
programs, and not a suite that passes, or fails for another reason.

## It worked before, or it works on another machine

Up to v3.x, `$*VM.name` answered `moar`; since v4.0.0 it answers `cpp`. A
LibraryMake installed before that stays installed, so a machine set up earlier
has it. From 4.0.0 until the installer learned to test it again as `moar`
(2026-10-30), a fresh machine could not install it without the variable.
`rakupp install --list` shows what a store already holds.

## Do I have to keep the variable set?

For LibraryMake, no: the installer takes care of its install. For a module that checks the
name every time it is loaded, yes, on every run. uniprop is one: it checks in a
`BEGIN` block, so `use uniprop` dies on Raku++ without the variable.

```sh
export RAKUPP_VM_NAME=moar      # for the rest of this shell session
```

With the variable set, uniprop installs, loads and answers:

```console
$ RAKUPP_VM_NAME=moar rakupp -e 'use uniprop; say uniprop-str("a", "Script")'
Latin
```

That needs a Raku++ newer than 5.4.1. In 5.4.1 the module loads, but its three
subs die with `X::Multi::NoMatch`: an engine bug in how they are declared,
since fixed. If you only need what uniprop's subs answer, the `uniprop` method
and sub built into the language do the same, on both engines and with no
variable:

```raku
say "a".uniprop("Script");             # Latin
say uniprop(0x31, "Numeric_Value");    # 1
```

## Which modules need it?

The ones known today:

| Module | Where it checks | With `RAKUPP_VM_NAME=moar` |
|---|---|---|
| LibraryMake | its own test suite | not needed: the installer tests it again as `moar` |
| uniprop | every `use` | loads and works; set it whenever the program runs |

NativeHelpers::Blob used to be on this list. The installer now ships its own
copy, and installs it as `provided by rakupp` with no variable needed.

If another module dies with a message like the ones at the top of this page,
look at the line it names. If that line checks `$*VM.name`, try the run with
`RAKUPP_VM_NAME=moar`, and report the module at
<https://github.com/ash/rakupp/issues> either way, so it can be listed here.
