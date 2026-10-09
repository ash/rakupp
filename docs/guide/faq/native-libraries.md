# FAQ — naming a native library so it loads on every OS

`is native('foo')` names a library, not a file. Each system spells the file
differently, so a name that loads on one machine can fail on the next. This
page is about writing the name once so that it loads on Linux, macOS and
Windows. [FFI.md](../FFI.md) covers the rest of NativeCall.

The snippets were run on macOS (Rakudo 2026.09) and on Linux (Rakudo 2022.12),
and under Raku++ on both. Windows was not run: its spellings below are read
from the two engines' sources. Where the engines differ, the page says so.

## The short answer

```raku
use NativeCall;
sub glib_check_version(uint32, uint32, uint32 --> Str) is native('glib-2.0', v0) {*}
```

- Write the name without `lib` and without an extension.
- Give the version: the number after `.so.` in the Linux file name.
- Don't write a file name such as `libglib-2.0.so.0`. That is a Linux file,
  and only a Linux machine has it.
- For Windows, or any library whose files break the pattern, choose the name
  per OS (see below).
- For functions from the C library itself (`strlen`, `pow`), leave the name
  out.

## What does a name turn into?

The engine turns a bare name into the platform's file name. Both engines spell
the Linux and macOS columns this way. The Windows column is Rakudo's; Raku++
differs there (see the last section).

| you write | Linux | macOS | Windows |
|---|---|---|---|
| `is native('glib-2.0')` | `libglib-2.0.so` | `libglib-2.0.dylib` | `glib-2.0.dll` |
| `is native('glib-2.0', v0)` | `libglib-2.0.so.0` | `libglib-2.0.0.dylib` | `glib-2.0.dll` |
| `is native('libglib-2.0.so.0')` | as written | as written | as written |
| `is native('/opt/lib/libfoo.so')` | as written | as written | as written |

A name that ends in an extension made of letters (`.so`, `.dylib`, `.dll`), or
in `.so.` and a number, is a file name and is used untouched. Everything else
is decorated. A Windows DLL name carries no version, so Rakudo drops it there.

Ask the engine what it will look for:

```raku
say $*VM.platform-library-name('glib-2.0'.IO);                 # "libglib-2.0.dylib".IO on macOS
say $*VM.platform-library-name('glib-2.0'.IO, :version(v0));   # "libglib-2.0.0.dylib".IO on macOS
```

On Linux the same two lines print `"libglib-2.0.so".IO` and
`"libglib-2.0.so.0".IO`.

Don't add the `lib` yourself. `is native('libglib-2.0')` makes Rakudo look for
`liblibglib-2.0.dylib`, and it does not load under Raku++ either.

## Why give the version?

On Linux, the file without a version, `libglib-2.0.so`, is a symlink that
comes with the development package (`libglib2.0-dev` on Debian and Ubuntu). A
machine that only runs programs has the versioned file and nothing else:

```sh
ls /usr/lib/x86_64-linux-gnu/libglib-2.0.so*
```

```
/usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
/usr/lib/x86_64-linux-gnu/libglib-2.0.so.0.8000.0
```

On that machine Rakudo cannot load `is native('glib-2.0')`:

```
Cannot locate native library 'libglib-2.0.so': libglib-2.0.so: cannot open shared object file: No such file or directory
```

`is native('glib-2.0', v0)` loads on both engines. Raku++ loads the first
form too: when the unversioned file is missing, it tries `libglib-2.0.so.8`
down to `.so.0`. A program that relies on that runs on Raku++ only, so give
the version.

The number is the one after `.so.` in the runtime file; `ldconfig -p` lists
them. Homebrew on macOS names the same library `libglib-2.0.0.dylib`. The two
numbers are usually the same, but check both:

```sh
ldconfig -p | grep libglib-2.0          # Linux
ls /opt/homebrew/lib/libglib-2.0.*      # macOS, Homebrew on Apple silicon
```

## When the files break the pattern: choose per OS

Neither engine can guess a DLL's name. GTK's Windows builds call GLib
`libglib-2.0-0.dll`, with the `lib` and the version, and the decoration gives
`glib-2.0.dll`. Name that file yourself:

```raku
use NativeCall;
constant GLIB = $*DISTRO.is-win ?? 'libglib-2.0-0.dll' !! ('glib-2.0', v0);
sub glib_check_version(uint32, uint32, uint32 --> Str) is native(GLIB) {*}
say glib_check_version(2, 0, 0);    # (Str) — NULL: the installed GLib is new enough
say glib_check_version(99, 0, 0);   # GLib version too old (major mismatch)
```

`('glib-2.0', v0)` is the two arguments of `is native('glib-2.0', v0)` held in
one constant. When macOS needs a name of its own, test
`$*KERNEL.name eq 'darwin'` the same way.

A `constant` is worked out when the file is compiled. That happens on the
machine that runs it, so `$*DISTRO` describes the right system.

## Let the user point at the file

Give `is native` a routine, and it is called at the first native call. Its
answer is used exactly as returned, so it must be a whole file name. A
`('glib-2.0', v0)` list returned from a routine fails on both engines.
`$*VM.platform-library-name` does the decorating:

```raku
use NativeCall;
sub glib {
    %*ENV<GLIB_LIBRARY>
      // ($*DISTRO.is-win ?? 'libglib-2.0-0.dll'
                          !! $*VM.platform-library-name('glib-2.0'.IO, :version(v0)).Str)
}
sub glib_check_version(uint32, uint32, uint32 --> Str) is native(&glib) {*}
say glib_check_version(2, 0, 0);    # (Str)
```

Whoever runs the program can now name the file when it is somewhere unusual,
without editing your code:

```sh
GLIB_LIBRARY=/opt/homebrew/lib/libglib-2.0.0.dylib rakudo prog.raku
```

## Functions from the C library itself

Leave the name out. `is native` with no argument looks in the running
program, which has the C library and the maths library loaded already:

```raku
use NativeCall;
sub strlen(Str --> size_t) is native {*}
sub pow(num64, num64 --> num64) is native {*}
say strlen('hello');    # 5
say pow(2e0, 10e0);     # 1024
```

Don't write `is native('m')`. On Linux `libm.so` is a linker script, not a
library, and Rakudo stops at it:

```
Cannot locate native library 'libm.so': /lib/x86_64-linux-gnu/libm.so: invalid ELF header
```

Raku++ goes on to `libm.so.6` and loads it.

## Where the system looks

The decorated name goes to the system's loader (`dlopen`, or `LoadLibrary` on
Windows), and the loader searches its own list of directories:

- **Linux**: the directories in `LD_LIBRARY_PATH`, then the ones `ldconfig`
  knows (`/etc/ld.so.conf`), then `/lib` and `/usr/lib`.
- **macOS**: the directories in `DYLD_LIBRARY_PATH`, then `/usr/lib` and the
  system's shared cache. Homebrew's `/opt/homebrew/lib` is not on the list.
- **Windows**: the program's own directory, the system directories, then
  `PATH`.

So a library somewhere else needs its directory named when the program runs:

```sh
LD_LIBRARY_PATH=$HOME/mylibs rakudo prog.raku            # Linux
DYLD_LIBRARY_PATH=/opt/homebrew/lib rakudo prog.raku     # macOS with Homebrew
```

A full path in the code, `is native('/opt/homebrew/lib/libglib-2.0.0.dylib')`,
works too. It ties the program to that one machine: fine for a script, wrong
for a module.

On macOS, `DYLD_LIBRARY_PATH` is dropped from the environment when a program
is started through a system binary such as `/usr/bin/env`. That is macOS's
System Integrity Protection, and it means a script that begins with
`#!/usr/bin/env rakudo` never sees the variable:

```sh
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./prog.raku          # via #!/usr/bin/env: dropped
DYLD_LIBRARY_PATH=/opt/homebrew/lib rakudo prog.raku     # passed on
```

Raku++ also searches Homebrew's directories itself, after the system has found
nothing: `/opt/homebrew/lib`, `/usr/local/lib`, and a keg-only formula's own
`opt/<formula>/lib`. A Homebrew library loads under `rakupp` with no
environment variable. Rakudo needs the variable.

## Shipping the library with your module

A distribution can carry its own compiled library. List it under `resources`
without decoration, put the platform's file in `resources/libraries/`, and
name it through `%?RESOURCES`:

```json
{ "name": "Answer", "version": "0.1", "auth": "zef:you",
  "provides": { "Answer": "lib/Answer.rakumod" },
  "resources": [ "libraries/answer" ] }
```

```raku
unit module Answer;
use NativeCall;
sub answer(--> int32) is native(%?RESOURCES<libraries/answer>) is export {*}
```

`libraries/answer` is decorated like any other name. The file in the
distribution is `resources/libraries/libanswer.so` on Linux,
`libanswer.dylib` on macOS and `answer.dll` on Windows. One compiled file runs
on one OS only, so a distribution usually ships the C source and builds the
library at install time with a `Build.rakumod`.

With `int answer(void) { return 42; }` compiled under the right name, this
prints 42 on both engines:

```sh
rakudo -I. -e 'use Answer; say answer()'
rakupp -I. -e 'use Answer; say answer()'
```

## Which file did it try?

The error names it, then gives the system loader's own list of the places it
looked. Rakudo quotes the decorated file, and Raku++ quotes the name as you
wrote it:

```
Cannot locate native library 'libno-such-lib.dylib': dlopen(libno-such-lib.dylib, …   # Rakudo
Cannot locate native library 'no-such-lib': dlopen(no-such-lib, …                       # Raku++
```

Both throw `X::AdHoc`. To fall back to another library, catch it by the start
of the message, which the two engines share:

```raku
use NativeCall;
sub nope(--> int32) is native('no-such-lib') {*}
try nope();
say $!.^name;                                                  # X::AdHoc
say $!.message.starts-with('Cannot locate native library');   # True
```

## Where the engines differ

Raku++ looks in more places than Rakudo, so a name can load under `rakupp` and
fail under `rakudo`. Code that has to run on both should follow Rakudo's
rules, and everything in the sections above does.

| | Rakudo | Raku++ |
|---|---|---|
| `is native('glib-2.0')` with only `libglib-2.0.so.0` installed (Linux) | not found | tries `.so.8` down to `.so.0`, loads `.so.0` |
| `is native('m')` on Linux | stops at the `libm.so` linker script | loads `libm.so.6` |
| a Homebrew library on macOS | needs `DYLD_LIBRARY_PATH` | found in Homebrew's directories |
| `is native('uuid')` on macOS, where `uuid_generate` is part of the system library | not found | loads |
| `$*VM.platform-library-name('libfoo'.IO)` | `liblibfoo.dylib` | `libfoo.dylib` |
| `$*VM.platform-library-name` of a path with a directory | a `Str` | an `IO::Path`; call `.Str` |
| `is native('cairo', v2)` on Windows (read from the source) | `cairo.dll` | `cairo-2.dll` |
| `is native('glib-2.0')` on Windows (read from the source) | `glib-2.0.dll` | `glib-2.0` as written; Windows adds `.dll` only to a name with no dot in it |
| the name in "Cannot locate native library" | the decorated file | the name as written |
