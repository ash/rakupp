# Installing Raku++

Every way to get a working `rakupp`, from the one-liner to the source build,
arranged by the machine you are sitting at. Which compiler and architecture to
prefer, per platform, is a separate page: [COMPILERS.md](COMPILERS.md).

## One command

**macOS, Linux, and the BSDs:**

```sh
curl -fsSL https://raku.online/install.sh | sh
```

**Windows**, in a PowerShell window:

```powershell
irm https://raw.githubusercontent.com/ash/rakupp/main/tools/install-windows.ps1 | iex
```

Either one downloads the release archive for the machine it is running on,
checks it against the published SHA-256, unpacks it into a per-user prefix,
asks whether you also want the engine under the second name `raku`, and puts
its `bin/` on your `PATH`. Neither needs root or an administrator. Re-running
either upgrades in place, and so does `rakupp upgrade`.

Then open a **new** terminal — a running one keeps the environment it started
with — and:

```sh
rakupp -e 'say 6 * 7'
```

The rest of this page is the detail: what each installer does, the other routes
per platform, and how to undo any of it.

## macOS

The one-liner above works with nothing installed. So does Homebrew, if you
already have it:

```sh
brew tap ash/rakupp
brew install rakupp        # or: brew install --HEAD rakupp   (latest main)
```

Apple Silicon installs a **prebuilt binary** (no compile); Intel builds from
source. Homebrew itself requires the Xcode Command Line Tools — if `brew install`
says to install them, run `xcode-select --install` first.

The two are separate installs and neither knows about the other: `brew upgrade
rakupp` updates the Homebrew one, `rakupp upgrade` updates the one-liner's. If
both are on your `PATH`, whichever comes first wins — the one-liner names the
other when it finds it, and `--prepend` puts its own first.

## Linux

```sh
curl -fsSL https://raku.online/install.sh | sh
```

x86-64 and ARM64 both have a prebuilt archive, so nothing is compiled. The
binary carries its own libstdc++, so there is no dependency to satisfy first
and no distribution version to match.

Two Linux cases have a route of their own rather than an archive:
[**Nix**](#nix--nixos), which cannot run a generic Linux binary at all, and
[**Guix**](#gnu-guix-linux), which has a channel.

## BSD

OpenBSD has a prebuilt archive, built in a CI virtual machine each release, and
the one-liner finds it:

```sh
curl -fsSL https://raku.online/install.sh | sh
```

On FreeBSD and NetBSD, [build from source](#build-from-source) — the one-liner
says so and prints the three commands rather than downloading something that
cannot run.

## The Unix installer in detail

`tools/install.sh` is POSIX `sh`, so it runs under dash on Debian, ash on
Alpine and `/bin/sh` on the BSDs. It writes nothing outside its prefix and your
shell startup files.

A pipe cannot pass arguments, so options go through `sh -s --`:

```sh
curl -fsSL https://raku.online/install.sh | sh -s -- --raku-alias --prepend
```

| Option | |
|---|---|
| `--version vX.Y.Z` | a particular release instead of the latest |
| `--dir DIR` | install somewhere else (default `$HOME/.rakupp`) |
| `--raku-alias` / `--no-raku-alias` | answer the `raku` question up front |
| `--prepend` | put `bin/` *first* on `PATH`, ahead of any other Raku |
| `--no-path` | leave your shell startup files alone |
| `--archive FILE` | install an archive you already downloaded |
| `--skip-checksum` | install without checking the download against its `.sha256` |
| `--uninstall` | remove the prefix and the `PATH` lines |
| `--yes` | never ask anything |

The same four decisions read from the environment, for a box where the
one-liner cannot take arguments: `RAKUPP_VERSION`, `RAKUPP_INSTALL_DIR`,
`RAKUPP_RAKU_ALIAS=1`, `RAKUPP_NO_PATH=1`.

**`PATH`.** The installer writes `<prefix>/env`, a small `sh` snippet that adds
`bin/` to `PATH` if it is not already there, and adds **one line** to each
shell startup file that applies — `~/.profile`, and `~/.zshrc`, `~/.bashrc`,
`~/.bash_profile` or `~/.config/fish/config.fish` as your shell requires:

```sh
. "$HOME/.rakupp/env"  # rakupp
```

Nothing else in those files is touched, and `--uninstall` removes exactly those
lines and leaves every other byte where it was. Re-running the installer adds
nothing a second time.

**The second name.** `rakupp` finds its runtime relative to its own executable's
real path, so `raku` inside `bin/` is a complete engine and not a stub —
`raku --exe prog.raku` compiles exactly as `rakupp --exe` does. The link is
relative, so the prefix can be moved afterwards. If a Rakudo `raku` is already
on your `PATH`, the installer names it and says which of the two will win;
`--prepend` swaps them.

**Undo.** The install leaves a copy of the script in the prefix:

```sh
sh "$HOME/.rakupp/install.sh" --uninstall
```

and the piped form needs nothing on disk:

```sh
curl -fsSL https://raku.online/install.sh | sh -s -- --uninstall
```

The one-liner is also served from the repository, for anyone who would rather
fetch it from GitHub than from the site — the two files are identical, and CI
checks that they stay so:

```sh
curl -fsSL https://raw.githubusercontent.com/ash/rakupp/main/tools/install.sh | sh
```

## Updating

```sh
rakupp upgrade             # replace this binary with the latest release
rakupp upgrade --check     # what is available; install nothing
```

`rakupp upgrade` is a Raku program carried **inside** the binary, like `rakupp
install` and `rakupp doc`, so it works the same on every platform and there is
no script on disk to keep in step with the engine. It replaces the prefix in
place: same location, same `PATH` entry, same `raku` name if you had one. It
verifies the download against the published SHA-256 before swapping anything,
and puts the old binary back if the new one does not run.

`--version vX.Y.Z` takes a particular release, including an older one;
`--force` reinstalls the version you already have.

It only updates an install one of the two installers made — the receipt they
leave, `<prefix>/rakupp-install.json`, is how it tells. Any other rakupp belongs
to whatever put it there, and `rakupp upgrade` names that instead of
overwriting it:

| Where it came from | What updates it |
|---|---|
| the one-liner, either platform | `rakupp upgrade`, or re-run the one-liner |
| Homebrew | `brew upgrade rakupp` |
| Nix | `nix profile upgrade rakupp` |
| Guix | `guix upgrade rakupp` |
| a source checkout | `git pull && cmake --build build` |

## Windows

Two installers, both per user, neither needing admin rights. They install to the
same place by default, so pick one per machine rather than layering them.

### The wizard

**[Download rakupp-setup-windows-x64.exe](https://github.com/ash/rakupp/releases/latest/download/rakupp-setup-windows-x64.exe)** — that link always
resolves to the latest release — and run it. It asks for the install location,
offers a checkbox for the second command name `raku`, and another for putting
`bin\` on your `PATH` — and it registers with Add/Remove Programs, so it comes
off the way any other Windows program does.

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

## Prebuilt archives

Every release ships six self-contained archives on the
[Releases page](https://github.com/ash/rakupp/releases/latest), each with a
`.sha256` beside it, and each built and smoke-tested in CI on the platform it
targets:

| Platform | Archive |
|---|---|
| macOS 11+, Apple Silicon and Intel | `rakupp-macos-universal.tar.gz` — one universal binary for both |
| Linux, x86-64 | `rakupp-linux-x86_64.tar.gz` — static libstdc++, no dependencies |
| Linux, ARM64 | `rakupp-linux-aarch64.tar.gz` — Raspberry Pi, Graviton, Ampere |
| OpenBSD, x86-64 | `rakupp-openbsd-x86_64.tar.gz` — base clang |
| Windows, x64 | `rakupp-windows-x64.zip` — MSVC, static CRT, no redistributable |
| Windows, x64 (MinGW) | `rakupp-windows-x64-mingw.zip` — for a MinGW-w64 / MSYS2 toolchain |

Every installer on this page is this section done for you — download, checksum,
unpack, `PATH`. To do it by hand, unpack keeping the `bin/ lib/ include/`
layout together (that's what `--exe` uses) and put `bin/` on your `PATH`.

**Anywhere else** — another BSD, a Linux architecture not listed, a system too
old for the archive — [build from source](#build-from-source). It needs CMake
and a C++17 compiler and nothing else: there are no third-party libraries to
find first, which is what keeps the list above short rather than a matrix.

`rakupp` locates the runtime library `--exe` needs relative to its own binary,
so it works from any directory whether run out of `build/` or from an install
prefix. If you copy the binary somewhere on its own, point it back with
`RAKUPP_HOME=<prefix>`.

`rakupp install` is a Raku program, `install.raku`, and it travels **inside**
the binary. So do `rakupp doc`, together with the two guides it reads, and
`rakupp upgrade`. There is no script to place, no path to get right, and no
version of a tool that can drift from the engine that dispatches to it:

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
