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

## Windows

Two installers, both per user, neither needing admin rights. They install to the
same place by default, so pick one per machine rather than layering them.

### The wizard

Download **`rakupp-setup-windows-x64.exe`** from the
[Releases page](https://github.com/ash/rakupp/releases/latest) and run it. It
asks for the install location, offers a checkbox for the second command name
`raku`, and another for putting `bin\` on your `PATH` — and it registers with
Add/Remove Programs, so it comes off the way any other Windows program does.

The file is **not code-signed**, so SmartScreen will call the publisher unknown
for as long as it takes the file to earn a reputation: **More info → Run
anyway**. The one-liner below is a script, and no SmartScreen check applies to
it.

It takes Inno Setup's own switches for unattended installs:

```powershell
rakupp-setup-windows-x64.exe /VERYSILENT /TASKS="rakualias,addtopath"
```

`/DIR="C:\rakupp"` picks the location, `/TASKS=""` takes neither checkbox, and
`/ALLUSERS` — from an elevated prompt — installs into Program Files and edits
the machine `PATH` instead of yours.

### One command in PowerShell

```powershell
irm https://raw.githubusercontent.com/ash/rakupp/main/tools/install-windows.ps1 | iex
```

In a **PowerShell** window — Start menu → "PowerShell", or the PowerShell tab of
Windows Terminal. `irm` and `iex` are PowerShell's own aliases for
`Invoke-RestMethod` and `Invoke-Expression`, so the line means nothing to
`cmd.exe` or to a Unix shell, which answer that there is no such command.

It downloads the latest `rakupp-windows-x64.zip`, checks it against the
published SHA-256, unpacks it into `%LOCALAPPDATA%\Programs\rakupp`, asks
whether you want the engine under the second name `raku` as well, and puts
`bin\` on your user `PATH`. Then open a *new* terminal — a running one keeps the
environment it started with.

`| iex` cannot pass arguments. To give the script options, hand it to a script
block instead:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/ash/rakupp/main/tools/install-windows.ps1))) -RakuAlias -Prepend
```

| Option | |
|---|---|
| `-Version v3.26.0` | a particular release instead of the latest |
| `-Dir C:\rakupp` | install somewhere else |
| `-RakuAlias` / `-NoRakuAlias` | answer the `raku` question up front |
| `-Prepend` | put `bin\` *first* on `PATH`, ahead of any other Raku |
| `-NoPath` | leave `PATH` alone |
| `-Archive <zip>` | install a zip you already downloaded |
| `-SkipChecksum` | install without checking the download against its `.sha256` |
| `-Uninstall` | remove the prefix and the `PATH` entry |
| `-Yes` | never ask anything |

The four decisions worth automating also read from the environment, for a box
where the one-liner cannot take arguments: `RAKUPP_VERSION`,
`RAKUPP_INSTALL_DIR`, `RAKUPP_RAKU_ALIAS=1`, `RAKUPP_NO_PATH=1`.

**The second name.** `rakupp` finds its runtime relative to its own executable,
so another name inside `bin\` is a complete engine, not a stub. The script makes
`raku.exe` a hard link to `rakupp.exe` (a copy, where the filesystem refuses
one); the wizard installs a second copy, which is what lets it own the file for
upgrades and rollback. If a Rakudo `raku` is already on your `PATH`, the script
names it and says which of the two will win; `-Prepend` swaps them.

Nothing is written outside the prefix and `HKCU\Environment`, so neither install
needs elevation — and both come off again. The script's last lines print the
undo command; normally that is the copy of itself it leaves in the prefix:

```powershell
& "$env:LOCALAPPDATA\Programs\rakupp\install-windows.ps1" -Uninstall
```

The script-block form does the same and needs nothing on disk:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/ash/rakupp/main/tools/install-windows.ps1))) -Uninstall
```

Either takes the prefix's entry back out of `PATH` and leaves the rest of the
value exactly as it found it, then deletes the prefix. The wizard's uninstaller,
in Add/Remove Programs, does the same for a wizard install.

A script *file* is also subject to the execution policy, which the piped
one-liner is not:

```powershell
powershell -ExecutionPolicy Bypass -File .\install-windows.ps1
```

## Prebuilt binaries (macOS, Linux, Windows)

Every release ships self-contained archives on the
[Releases page](https://github.com/ash/rakupp/releases/latest):
`rakupp-macos-universal.tar.gz` (Apple Silicon + Intel, macOS 11+),
`rakupp-linux-x86_64.tar.gz` (static libstdc++ — no dependencies), and
`rakupp-windows-x64.zip` (static CRT — no redistributable needed). Unpack
keeping the `bin/ lib/ include/` layout together (that's what `--exe` uses)
and put `bin/` on your `PATH`. On Windows, [either installer above](#windows) is
this section done for you — download, checksum, unpack, `PATH`.

`rakupp` locates the runtime library `--exe` needs relative to its own binary,
so it works from any directory whether run out of `build/` or from an install
prefix. If you copy the binary somewhere on its own, point it back with
`RAKUPP_HOME=<prefix>`.

`rakupp install` is a Raku program, `install.raku`, and it travels **inside**
the binary. So does `rakupp doc`, together with the two guides it reads. There
is no script to place, no path to get right, and no version of the installer
that can drift from the engine that dispatches to it:

```dockerfile
COPY bin/rakupp /usr/local/bin/rakupp
# that is the whole installer — `rakupp install Foo` works from here
```

The same goes for a lone `rakupp.exe` lifted out of the Windows ZIP, and for
any packaging route that ships `bin/` without `libexec/` — Homebrew's prebuilt
macOS binary does, and it no longer matters.

Up to and including v3.26.0 the script was shipped beside the binary in
`libexec/rakupp/install.raku`, and a binary on its own answered "cannot find
install.raku beside this binary". Nothing is written to `libexec/rakupp/` any
more; a copy left there by an older install is read by nothing, and can be
deleted.

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
([PR #6](https://github.com/ash/rakupp/pull/6), contributed from outside the
project). Build directly from a checkout:

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
