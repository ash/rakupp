# Packaging rakupp for Homebrew

rakupp ships as a Homebrew formula ([`rakupp.rb`](rakupp.rb)). Because it is a
clean CMake project with proper `install()` rules and no third-party
dependencies, the formula is minimal: `depends_on "cmake" => :build`, then
`cmake` build + install.

## The realistic route: a personal tap

Homebrew **Core** (`brew install rakupp`) requires a public, *notable* project
(~30+ stars/forks) and a formula PR to `homebrew/homebrew-core` — a later goal.
For now, publish through a **personal tap**, which anyone can add:

```sh
brew tap ash/rakupp        # adds github.com/ash/homebrew-rakupp
brew install rakupp
```

### One-time setup

1. **Make `rakupp` reachable.** A formula fetches a URL, so the source (or at
   least the release tarball) must be downloadable. A fully private repo can't be
   tapped by others; your own machine can still `--HEAD`-install over SSH.

2. **Create the tap repo.** It MUST be named with the `homebrew-` prefix:

   ```sh
   gh repo create ash/homebrew-rakupp --public --clone
   mkdir -p homebrew-rakupp/Formula
   cp packaging/rakupp.rb homebrew-rakupp/Formula/rakupp.rb
   cd homebrew-rakupp && git add Formula/rakupp.rb && git commit -m "rakupp formula" && git push
   ```

3. **Cut a versioned release** (formulae pin a tarball, not a branch):

   ```sh
   git tag v0.1.0 && git push origin v0.1.0
   # GitHub auto-generates the source tarball; grab its checksum:
   curl -sL https://github.com/ash/rakupp/archive/refs/tags/v0.1.0.tar.gz | shasum -a 256
   ```

   Paste that digest into `sha256` in the formula (and bump `url`/`sha256` on each
   new release). `VERSION` in `CMakeLists.txt` is the single source of truth for
   the version number — keep the tag in sync.

## Installing before a release

The formula carries a `head` line, so you can build straight from `main` without
any tag:

```sh
brew install --HEAD ash/rakupp/rakupp
```

## Testing the formula locally

```sh
brew install --build-from-source ./packaging/rakupp.rb   # build it
brew test rakupp                                          # runs the `test do` block
brew audit --new --strict rakupp                          # style + correctness lint
```

## How `--exe` keeps working after install

`cmake --install` lays down `bin/rakupp`, `lib/librakupp_rt.a`, and
`include/rakupp/*.h` in the Cellar; Homebrew symlinks `bin/rakupp` onto `PATH`.
`findRuntime()` resolves the *real* (symlink-resolved) executable path back into
the Cellar and finds `lib/` and `include/rakupp/` as siblings — so native
compilation (`rakupp --exe`) works with no `RAKUPP_HOME` needed.
