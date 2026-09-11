# INSTALL-PLAN — one command per platform

Windows got a real engine installer in v3.27.0: `tools/install-windows.ps1` and
the Inno Setup wizard, both per-user, both putting `bin\` on `PATH`, both
offering the second name `raku`, both upgrading in place when re-run. Nothing
outside Windows had any of that. macOS had the Homebrew tap, which assumes
Homebrew; Linux and the BSDs had one paragraph telling people to unpack a
tarball and edit their own `PATH`, with no `raku` name, no upgrade path and no
uninstall. ARM Linux had no archive at all.

This plan closes that. Status: **landed**, in the six commits below.

## Decisions

| | |
|---|---|
| Headline URL | `curl -fsSL https://raku.online/install.sh \| sh`, with the `raw.githubusercontent.com` URL documented as the fallback |
| Updating | both — re-running the one-liner upgrades in place, **and** `rakupp upgrade` |
| ARM Linux | `linux-aarch64` added to the release matrix |
| Breadth | the script, Homebrew, and the existing Guix and Nix routes |

## What landed

1. **`tools/install.sh`** — POSIX `sh` (dash, ash, BSD `/bin/sh`), mirroring
   `install-windows.ps1` option for option. Detects the platform, downloads
   the release archive, verifies the published SHA-256, unpacks into
   `$HOME/.rakupp`, offers the `raku` symlink, and adds one line per shell
   startup file sourcing a generated `<prefix>/env`. Leaves a receipt,
   `<prefix>/rakupp-install.json`, and a copy of itself for `--uninstall`.
2. **`rakupp upgrade`** — `tools/upgrade.raku`, embedded by
   `cmake/EmbedTools.cmake` and dispatched from `src/main.cpp` exactly as
   `install.raku` and `doc.raku` are. Reads the receipt, refuses any prefix the
   installers did not make, and names that package manager's own command
   instead.
3. **`linux-aarch64`** — one line in the `release.yml` matrix, on
   `ubuntu-24.04-arm`.
4. **Gates** — `t/unix/install.sh` (25 TAP checks, the POSIX sibling of
   `t/windows/install.ps1`, wired into `release.yml` on every non-Windows leg)
   and `t/regression/rakupp-upgrade.raku` (network-free: version ordering and
   the refusals). Plus an `installer-copy` job comparing `tools/install.sh`
   against what raku.online actually serves.
5. **Docs** — `docs/guide/INSTALL.md` rearranged by operating system, README,
   and the `RELEASING.md` reminders.
6. **raku.online** — `www/install.sh`, and the one-liners on `/install/` and
   the front page.

## Four things worth not rediscovering

- **A POSIX `raku` symlink is a complete engine.** `selfExePath`
  (`src/main.cpp`) resolves through it — `_NSGetExecutablePath` + `realpath`,
  `/proc/self/exe` on Linux — so `raku --exe` compiles. The hard-link dance in
  the `.ps1` is a Windows-only concern with no POSIX equivalent.
- **`ln`, not Raku's `symlink`, for a RELATIVE link.** `symlink('rakupp', …)`
  resolves the target against the **cwd** and stores the result, so the link
  names wherever the process happened to be standing. Rakudo does the same; it
  is the language, not a divergence. `ln -s` run inside `bin/` stores the
  target verbatim, which is what makes the prefix relocatable.
- **`try` is a statement PREFIX.** `$x = try EXPR unless $x` does not skip the
  assignment: the modifier binds *inside* the `try`, and the skipped
  statement's empty `Slip` is assigned over whatever `$x` held. Both engines
  agree. Use an `unless` block.
- **`/dev/tty` can pass `test -r` and still refuse `open(2)`.** A process with
  no controlling terminal — a CI step, `docker run` without `-t`, cron — has
  exactly that. Probe by opening it in a subshell, or every question writes
  "Device not configured" at the user and takes its default anyway.

## Out of scope, decided rather than forgotten

- **Scoop and WinGet manifests.** Each is a manifest in a separate repo plus a
  per-release bump — a third and fourth thing to forget on release day, beside
  the Homebrew tap that already gets forgotten.
- **`.deb` / `.rpm` / AUR packages.** Repo hosting, GPG signing, per-distro
  conventions and an AUR maintainer relationship. Its own campaign.
- **macOS code signing and notarization.** The one-liner is a script, so no
  Gatekeeper check applies to it; the Windows wizard's SmartScreen note in
  INSTALL.md is the equivalent disclosure there.
- **Docker images.** `COPY bin/rakupp /usr/local/bin/rakupp` is already the
  whole installation, and INSTALL.md says so.
