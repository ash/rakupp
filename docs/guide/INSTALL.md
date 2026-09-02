# Installing Raku++

Every way to get a working `rakupp`, from the one-liner to the source build.
Which compiler and architecture to prefer, per platform, is a separate page:
[COMPILERS.md](COMPILERS.md).

## Homebrew (macOS)

```sh
brew tap ash/rakupp
brew install rakupp        # or: brew install --HEAD rakupp   (latest main)
```

Apple Silicon installs a **prebuilt binary** (no compile); Intel builds from
source. Homebrew itself requires the Xcode Command Line Tools — if `brew install`
says to install them, run `xcode-select --install` first.

## Prebuilt binaries (macOS, Linux, Windows)

Every release ships self-contained archives on the
[Releases page](https://github.com/ash/rakupp/releases/latest):
`rakupp-macos-universal.tar.gz` (Apple Silicon + Intel, macOS 11+),
`rakupp-linux-x86_64.tar.gz` (static libstdc++ — no dependencies), and
`rakupp-windows-x64.zip` (static CRT — no redistributable needed). Unpack
keeping the `bin/ lib/ include/` layout together (that's what `--exe` uses)
and put `bin/` on your `PATH`.

`rakupp` locates the runtime library `--exe` needs relative to its own binary,
so it works from any directory whether run out of `build/` or from an install
prefix. If you copy the binary somewhere on its own, point it back with
`RAKUPP_HOME=<prefix>`.

`rakupp install` is a Raku program, `install.raku`, shipped **beside** the
binary rather than inside it: an install layout has it at
`libexec/rakupp/install.raku` (every release archive and `cmake --install`
include it), a checkout at `tools/install.raku`. The binary looks in exactly
those two places relative to itself and nowhere else — `RAKUPP_HOME` does not
cover it — and without the file `rakupp install` stops with "cannot find
install.raku beside this binary". So when you ship the binary alone, into a
container say, put the script from the **same release** back beside it:

```dockerfile
COPY bin/rakupp                  /usr/local/bin/rakupp
COPY libexec/rakupp/install.raku /usr/local/libexec/rakupp/install.raku
```

Homebrew's prebuilt-binary route (macOS) currently drops `libexec/` too, so a
`brew install rakupp` has no `rakupp install` until the tap ships that
directory; the formula's source build (`cmake --install`) is unaffected.

## Build from source

```sh
# Needs a C++17 compiler + CMake → produces build/rakupp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Install onto $PATH (binary + the runtime that --exe links against)
cmake --install build --prefix ~/.local   # → ~/.local/{bin,lib,include/rakupp}
```

On Windows (MSVC), build from a *Developer Command Prompt* and pass the
configuration to the build step — the Visual Studio generator is
multi-config, so `-DCMAKE_BUILD_TYPE` alone is not enough:

```sh
cmake -S . -B build
cmake --build build --config Release      # → build/Release/rakupp.exe
```

## GNU Guix (Linux)

The repository is also a Guix channel
([PR #6](https://github.com/ash/rakupp/pull/6), contributed by
[@4zv4l](https://github.com/4zv4l)). Build directly from a checkout:

```sh
guix build -f .guix/modules/rakupp-package.scm
```

or add the channel to `~/.config/guix/channels.scm` and install:

```scm
(channel
  (name 'rakupp)
  (url "https://github.com/ash/rakupp")
  (branch "main"))
```

```sh
guix pull && guix install rakupp
```

## Nix / NixOS

NixOS can't run the generic prebuilt Linux binary (it has no global ELF
interpreter — [issue #5](https://github.com/ash/rakupp/issues/5)), so build
from source through the repository's flake:

```sh
nix run github:ash/rakupp -- -e 'say 42'
```

```sh
nix profile install github:ash/rakupp
```

From a checkout, `nix build` produces `./result/bin/rakupp`.
