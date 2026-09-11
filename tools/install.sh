#!/bin/sh
# Install Raku++ (rakupp) on macOS, Linux and the BSDs: per user, no root, on
# PATH.
#
#   curl -fsSL https://raku.online/install.sh | sh
#
# Downloads a release archive for this platform, checks it against the
# published SHA-256, unpacks it into a per-user prefix, offers the engine under
# the second name `raku`, and puts the prefix's bin/ on PATH by sourcing a
# generated env file from your shell's startup file. Nothing is written outside
# the prefix and those startup files, so no root is needed and the whole thing
# comes off again with --uninstall.
#
# This is the ENGINE installer, and the POSIX counterpart of
# tools/install-windows.ps1 -- one design in two spellings. `rakupp install
# Foo` -- the MODULE installer, tools/install.raku -- is a different program
# that travels inside the binary. `rakupp upgrade` -- tools/upgrade.raku, also
# inside the binary -- is what updates an install this script made.
#
# POSIX sh, not bash: this has to run under dash on Debian, ash on Alpine and
# /bin/sh on the BSDs, on a machine that has nothing installed yet.
#
# Everything below is a function until the last line, so a download that breaks
# off half way defines some functions and runs none of them.

set -eu

REPO='ash/rakupp'
MARK='# rakupp'          # the marker that makes a line of ours findable again
SITE='https://raku.online/install.sh'

usage() {
    cat <<'EOT'
Install Raku++ (rakupp): per user, no root, on PATH.

    curl -fsSL https://raku.online/install.sh | sh

A pipe cannot pass arguments, so either use `sh -s --`:

    curl -fsSL https://raku.online/install.sh | sh -s -- --raku-alias

or set the environment variables at the bottom.

  --version vX.Y.Z   a particular release instead of the latest
  --dir DIR          install prefix (default: $HOME/.rakupp)
  --raku-alias       install the second name `raku` without asking
  --no-raku-alias    do not install it, and do not ask
  --prepend          put bin/ FIRST on PATH, ahead of any other Raku
  --no-path          leave the shell startup files alone
  --archive FILE     install an archive already on disk
  --skip-checksum    install without checking the download against its .sha256
  --uninstall        remove the prefix and the PATH lines
  --yes              never ask anything
  --help             this text

  RAKUPP_VERSION  RAKUPP_INSTALL_DIR  RAKUPP_RAKU_ALIAS=1  RAKUPP_NO_PATH=1

Updating: re-run this script, or `rakupp upgrade`.
EOT
    exit 0
}

# ---------------------------------------------------------------------------
# Saying things. The same two shapes the PowerShell installer prints, so the
# two read alike in a bug report.
# ---------------------------------------------------------------------------
say()  { printf '==> %s\n' "$*"; }
note() { printf '    %s\n' "$*"; }
fail() { printf 'rakupp install: %s\n' "$*" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# An explicit flag always wins over the variable; these are consulted only when
# no flag said otherwise.
env_is_yes() {
    eval "_v=\${$1-}"
    case "$(printf '%s' "${_v-}" | tr '[:upper:]' '[:lower:]')" in
        1|y|yes|true|on) return 0 ;; *) return 1 ;;
    esac
}
env_is_no() {
    eval "_v=\${$1-}"
    case "$(printf '%s' "${_v-}" | tr '[:upper:]' '[:lower:]')" in
        0|n|no|false|off) return 0 ;; *) return 1 ;;
    esac
}

# ---------------------------------------------------------------------------
# Asking. Under `curl ... | sh` the SCRIPT is standard input, so a plain `read`
# would eat the rest of this file instead of the user's answer. Every question
# goes to /dev/tty, and where there is no tty there is no question -- the same
# "nobody to ask" path the PowerShell installer takes in CI.
# ---------------------------------------------------------------------------
# OPENING it, not merely stat-ing it: a process with no controlling terminal --
# a CI step, a `docker run` without -t, a cron job -- has a /dev/tty that `test
# -r` says yes to and open(2) then refuses with ENXIO. Testing the permission
# bits would leave every question here writing "Device not configured" to the
# user and taking its default anyway. The open happens in a subshell, so no
# descriptor outlives the probe.
INTERACTIVE=0
if (exec 3<>/dev/tty) 2>/dev/null; then INTERACTIVE=1; fi

# ask "<question> [Y/n]" <default: y|n> -> 0 for yes
ask() {
    _q=$1; _def=$2
    if [ "$INTERACTIVE" != 1 ]; then [ "$_def" = y ]; return; fi
    printf '%s ' "$_q" > /dev/tty 2>/dev/null || { [ "$_def" = y ]; return; }
    IFS= read -r _a < /dev/tty 2>/dev/null || _a=''
    case "$(printf '%s' "$_a" | tr '[:upper:]' '[:lower:]')" in
        y|yes) return 0 ;;
        n|no)  return 1 ;;
        *)     [ "$_def" = y ]; return ;;
    esac
}

# ---------------------------------------------------------------------------
# Which archive this machine wants. An unknown platform is not a crash: it
# prints the source build and stops, because someone who typed a one-liner did
# not ask for a ten-minute compile.
# ---------------------------------------------------------------------------
detect_asset() {
    _os=$(uname -s 2>/dev/null || echo unknown)
    _arch=$(uname -m 2>/dev/null || echo unknown)
    case "$_os" in
        Darwin)
            # One universal binary covers both architectures, so uname -m is
            # not consulted here: an Intel Mac and an Apple Silicon Mac take
            # the same file.
            echo 'rakupp-macos-universal.tar.gz' ;;
        Linux)
            case "$_arch" in
                x86_64|amd64)  echo 'rakupp-linux-x86_64.tar.gz' ;;
                aarch64|arm64) echo 'rakupp-linux-aarch64.tar.gz' ;;
                *) echo '' ;;
            esac ;;
        OpenBSD)
            case "$_arch" in
                x86_64|amd64) echo 'rakupp-openbsd-x86_64.tar.gz' ;;
                *) echo '' ;;
            esac ;;
        *) echo '' ;;
    esac
}

no_binary_for_this_machine() {
    cat >&2 <<EOT
rakupp install: no prebuilt archive for $(uname -s 2>/dev/null || echo '?') $(uname -m 2>/dev/null || echo '?').

    Raku++ has no third-party dependencies, so building it needs CMake and a
    C++17 compiler and nothing else:

        git clone https://github.com/$REPO
        cd rakupp
        cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
        cmake --build build
        cmake --install build --prefix "\$HOME/.local"

    The platforms that do ship an archive are listed at
    https://raku.online/install/
EOT
    exit 1
}

# ---------------------------------------------------------------------------
# Fetching, and proving what was fetched.
# ---------------------------------------------------------------------------
fetch_to() { # url dest
    if have curl; then
        # --proto '=https' refuses a redirect that leaves TLS; --fail turns an
        # HTTP error into a non-zero exit instead of a saved error page.
        curl -fsSL --proto '=https' --tlsv1.2 -o "$2" "$1"
    elif have wget; then
        wget -q -O "$2" "$1"
    else
        fail 'neither curl nor wget is installed -- one of them is needed to download the release'
    fi
}

sha256_of() { # file -> lowercase hex, or empty when no tool on this box can
    if   have shasum;    then shasum -a 256 "$1" | awk '{print $1}'
    elif have sha256sum; then sha256sum "$1"     | awk '{print $1}'
    elif have openssl;   then openssl dgst -sha256 "$1" | awk '{print $NF}'
    else echo ''
    fi
}

# ---------------------------------------------------------------------------
# PATH, through an env file rather than a PATH= line per startup file. One
# generated file holds the logic, each startup file gets one line that sources
# it, and --uninstall removes exactly those lines -- which is what lets this
# leave every other byte of a dotfile alone.
# ---------------------------------------------------------------------------

# /home/me/.rakupp/env -> $HOME/.rakupp/env, as LITERAL text, so the line
# written into a dotfile survives a home directory that later moves.
homeish() {
    case "$1" in
        "$HOME"/*) printf '$HOME/%s' "${1#"$HOME"/}" ;;
        *)         printf '%s' "$1" ;;
    esac
}

# The startup files to write to, one per line. Each is listed when it already
# exists, and additionally when it is the one this user's login shell actually
# reads -- a fresh macOS account has no ~/.zshrc, and zsh never reads
# ~/.profile, so writing only to files that exist would leave PATH unset on
# exactly the machine the one-liner is most likely typed into.
rc_files() {
    _sh=$(basename "${SHELL:-sh}" 2>/dev/null || echo sh)
    _zdot=${ZDOTDIR:-$HOME}

    # sh, dash, ksh, and a login bash with no ~/.bash_profile, all read this.
    echo "$HOME/.profile"

    if [ "$_sh" = zsh ] || [ -f "$_zdot/.zshrc" ]; then echo "$_zdot/.zshrc"; fi
    if [ "$_sh" = bash ] || [ -f "$HOME/.bashrc" ]; then echo "$HOME/.bashrc"; fi
    # bash reads ~/.bash_profile INSTEAD of ~/.profile when it exists, so the
    # ~/.profile line above would be dead for a login bash on such a machine.
    if [ -f "$HOME/.bash_profile" ]; then echo "$HOME/.bash_profile"; fi
    if [ "$_sh" = fish ] || [ -d "$HOME/.config/fish" ]; then
        echo "$HOME/.config/fish/config.fish"
    fi
}

is_fish_rc() { case "$1" in */config.fish) return 0 ;; *) return 1 ;; esac; }

# The exact line written into a startup file. Generated in one place so that
# --uninstall regenerates the identical string and can match it literally.
rc_line_for() { # rcfile
    if is_fish_rc "$1"; then
        printf 'source "%s"  %s\n' "$(homeish "$PREFIX/env.fish")" "$MARK"
    else
        printf '. "%s"  %s\n' "$(homeish "$PREFIX/env")" "$MARK"
    fi
}

write_env_files() {
    _bin=$(homeish "$PREFIX/bin")
    if [ "$PREPEND" = 1 ]; then
        _posix_set='PATH="${__rakupp_bin}${PATH:+:${PATH}}"'
        _fish_set='set -gx PATH $__rakupp_bin $PATH'
    else
        _posix_set='PATH="${PATH:+${PATH}:}${__rakupp_bin}"'
        _fish_set='set -gx PATH $PATH $__rakupp_bin'
    fi

    cat > "$PREFIX/env" <<EOT
# rakupp shell setup -- generated by install.sh and sourced from your shell's
# startup file. Adds rakupp's bin/ to PATH, and does nothing when it is already
# there, so sourcing it twice is harmless.
__rakupp_bin="$_bin"
case ":\${PATH}:" in
    *":\${__rakupp_bin}:"*) ;;
    *) $_posix_set ; export PATH ;;
esac
unset __rakupp_bin
EOT

    cat > "$PREFIX/env.fish" <<EOT
# rakupp shell setup for fish -- generated by install.sh.
set -l __rakupp_bin "$_bin"
if not contains -- \$__rakupp_bin \$PATH
    $_fish_set
end
EOT
}

# Append our line, once. A dotfile that does not end in a newline would
# otherwise get our line glued onto its last one.
rc_add() { # rcfile -> 0 when it added one, 1 when it was already there
    _rc=$1
    _line=$(rc_line_for "$_rc")
    if [ -f "$_rc" ] && grep -qxF "$_line" "$_rc" 2>/dev/null; then return 1; fi
    mkdir -p "$(dirname "$_rc")"
    if [ -s "$_rc" ] && [ -n "$(tail -c 1 "$_rc" 2>/dev/null || true)" ]; then
        printf '\n' >> "$_rc"
    fi
    printf '%s\n' "$_line" >> "$_rc"
    return 0
}

# Remove our line, and only our line. grep -v -x -F matches a whole line
# literally, so a startup file that mentions rakupp anywhere else keeps every
# word of it.
rc_remove() { # rcfile -> 0 when it removed one
    _rc=$1
    [ -f "$_rc" ] || return 1
    _line=$(rc_line_for "$_rc")
    grep -qxF "$_line" "$_rc" 2>/dev/null || return 1
    _tmp="$_rc.rakupp-tmp.$$"
    grep -vxF "$_line" "$_rc" > "$_tmp" || true
    # cat over the original rather than renaming the temp into place, so a
    # dotfile that was mode 600 stays mode 600.
    cat "$_tmp" > "$_rc"
    rm -f "$_tmp"
    return 0
}

# ---------------------------------------------------------------------------
# The receipt. What was installed, how, and which startup files were touched --
# read by --uninstall here, and by `rakupp upgrade` in the engine, whose first
# job is to refuse a prefix this script did not make.
# ---------------------------------------------------------------------------
json_str() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'; }

write_receipt() { # version asset true|false <newline-separated rc files>
    # Locals with names of their own: a shell function shares the caller's
    # variables, and an _alias here would overwrite do_install's.
    _ver=$1; _rasset=$2; _raliasval=$3; _rclist=${4-}
    _rcs=''
    _oldifs=$IFS; IFS='
'
    for _r in $_rclist; do
        if [ -n "$_rcs" ]; then _rcs="$_rcs, "; fi
        _rcs="$_rcs\"$(json_str "$_r")\""
    done
    IFS=$_oldifs
    cat > "$PREFIX/rakupp-install.json" <<EOT
{
  "installed_by": "install.sh",
  "version": "$(json_str "$_ver")",
  "asset": "$(json_str "$_rasset")",
  "prefix": "$(json_str "$PREFIX")",
  "repo": "$REPO",
  "raku_symlink": $_raliasval,
  "rc_files": [$_rcs],
  "installed_at": "$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo unknown)"
}
EOT
}

# The rc files a previous run recorded, one per line. Read with sed rather than
# a JSON parser because this script cannot assume one exists -- and the file it
# is reading was written, on one line, by this same script.
receipt_rc_files() {
    [ -f "$PREFIX/rakupp-install.json" ] || return 0
    sed -n 's/.*"rc_files"[[:space:]]*:[[:space:]]*\[\(.*\)\].*/\1/p' \
        "$PREFIX/rakupp-install.json" \
        | tr ',' '\n' \
        | sed -e 's/^[[:space:]]*"//' -e 's/"[[:space:]]*$//' \
        | grep -v '^[[:space:]]*$' || true
}

receipt_has_alias() {
    [ -f "$PREFIX/rakupp-install.json" ] || return 1
    grep -q '"raku_symlink"[[:space:]]*:[[:space:]]*true' "$PREFIX/rakupp-install.json"
}

# ---------------------------------------------------------------------------
other_on_path() { # command name -> its path, when it is not one of ours
    _found=$(command -v "$1" 2>/dev/null || true)
    [ -n "$_found" ] || return 1
    case "$_found" in "$PREFIX"/*) return 1 ;; esac
    printf '%s' "$_found"
}

looks_like_brew() {
    case "$1" in */Cellar/*|*/homebrew/*|*/linuxbrew/*) return 0 ;; *) return 1 ;; esac
}

# ---------------------------------------------------------------------------
main() {
    VERSION=''; DIR=''; ARCHIVE=''
    WANT_ALIAS=''; NO_PATH=0; PREPEND=0; SKIP_SUM=0; UNINSTALL=0; YES=0

    # `shift 2` on an option that was given no value is a shell error, not a
    # message -- dash says "shift: can't shift that many" and every other shell
    # says something else. Check first and name the option.
    need() { [ "$1" -ge 2 ] || fail "$2 needs a value"; }

    while [ $# -gt 0 ]; do
        case "$1" in
            --version)       need $# --version; VERSION=$2; shift 2 ;;
            --version=*)     VERSION=${1#*=}; shift ;;
            --dir|--prefix)  need $# "$1"; DIR=$2; shift 2 ;;
            --dir=*|--prefix=*) DIR=${1#*=}; shift ;;
            --archive)       need $# --archive; ARCHIVE=$2; shift 2 ;;
            --archive=*)     ARCHIVE=${1#*=}; shift ;;
            --raku-alias)    WANT_ALIAS=1; shift ;;
            --no-raku-alias) WANT_ALIAS=0; shift ;;
            --prepend)       PREPEND=1; shift ;;
            --no-path)       NO_PATH=1; shift ;;
            --skip-checksum) SKIP_SUM=1; shift ;;
            --uninstall)     UNINSTALL=1; shift ;;
            -y|--yes)        YES=1; shift ;;
            -h|--help)       usage ;;
            *) fail "unknown option: $1 (--help lists them)" ;;
        esac
    done

    if [ "$YES" = 1 ]; then INTERACTIVE=0; fi

    # A pipe cannot pass arguments, so the four decisions worth automating are
    # also readable from the environment. A flag always wins.
    if [ -z "$DIR" ];     then DIR=${RAKUPP_INSTALL_DIR-}; fi
    if [ -z "$VERSION" ]; then VERSION=${RAKUPP_VERSION-}; fi
    if [ "$NO_PATH" = 0 ] && env_is_yes RAKUPP_NO_PATH; then NO_PATH=1; fi
    if [ -z "$WANT_ALIAS" ]; then
        if   env_is_yes RAKUPP_RAKU_ALIAS; then WANT_ALIAS=1
        elif env_is_no  RAKUPP_RAKU_ALIAS; then WANT_ALIAS=0
        fi
    fi

    [ -n "${HOME-}" ] || fail 'HOME is not set -- pass --dir'
    if [ -z "$DIR" ]; then DIR="$HOME/.rakupp"; fi
    # An unrooted --dir means "here", where the user typed it.
    case "$DIR" in /*) ;; *) DIR="$PWD/$DIR" ;; esac
    PREFIX=$(printf '%s' "$DIR" | sed 's:/*$::')
    BIN="$PREFIX/bin"
    EXE="$BIN/rakupp"
    ALIAS="$BIN/raku"

    if [ "$UNINSTALL" = 1 ]; then do_uninstall; else do_install; fi
}

# ---------------------------------------------------------------------------
do_uninstall() {
    # Everything that can say no says it BEFORE anything is touched: --dir can
    # be mistyped, and deleting is the one step here with no undo. So is the
    # question -- answering it after half the work is done would leave the
    # startup files stripped and the files in place.
    if [ -e "$PREFIX" ] && [ ! -x "$EXE" ]; then
        fail "$PREFIX holds no bin/rakupp -- refusing to delete it. Remove it by hand if that is what you meant."
    fi
    if [ -e "$PREFIX" ] && [ "$YES" != 1 ]; then
        [ "$INTERACTIVE" = 1 ] || fail "re-run with --yes to delete $PREFIX"
        if ! ask "Remove $PREFIX and its PATH lines? [y/N]" n; then
            say 'nothing changed'; return
        fi
    fi

    # The startup files first, then the prefix: an uninstall that breaks off
    # half way leaves a line pointing at a directory that is still there,
    # rather than one pointing at nothing.
    #
    # The recorded files AND today's candidates: a startup file created since
    # the install still gets checked, and one recorded then but deleted since
    # is simply not found.
    _any=0
    _oldifs=$IFS; IFS='
'
    for _rc in $(receipt_rc_files) $(rc_files); do
        IFS=$_oldifs
        if rc_remove "$_rc"; then say "removed the rakupp line from $_rc"; _any=1; fi
        IFS='
'
    done
    IFS=$_oldifs
    [ "$_any" = 1 ] || say 'no rakupp line in any shell startup file'

    if [ -e "$PREFIX" ]; then
        rm -rf "$PREFIX"
        say "deleted $PREFIX"
    else
        say "nothing installed at $PREFIX"
    fi
    note 'open a new terminal, or the PATH of this one still names the prefix'
}

# ---------------------------------------------------------------------------
do_install() {
    if [ -n "$ARCHIVE" ]; then
        [ -f "$ARCHIVE" ] || fail "no such archive: $ARCHIVE"
        case "$ARCHIVE" in /*) ;; *) ARCHIVE="$PWD/$ARCHIVE" ;; esac
        ASSET=$(basename "$ARCHIVE")
    else
        ASSET=$(detect_asset)
        [ -n "$ASSET" ] || no_binary_for_this_machine
    fi

    # Read before the unpack overwrites it: an upgrade keeps the name it had.
    HAD_ALIAS=0
    if [ -e "$ALIAS" ] || receipt_has_alias; then HAD_ALIAS=1; fi

    TMP=$(mktemp -d "${TMPDIR:-/tmp}/rakupp-install.XXXXXX") \
        || fail 'cannot make a temporary directory'
    trap 'rm -rf "$TMP"' EXIT INT TERM

    if [ -n "$ARCHIVE" ]; then
        TARBALL=$ARCHIVE
        say "installing from $TARBALL"
    else
        # The /releases/latest/download/ and /releases/download/<tag>/
        # redirects need no API call: no token, and no 60-per-hour rate limit
        # to run into on a shared address.
        if [ -n "$VERSION" ]; then
            _tag=$VERSION
            case "$_tag" in v*) ;; *) _tag="v$_tag" ;; esac
            URL="https://github.com/$REPO/releases/download/$_tag/$ASSET"
        else
            URL="https://github.com/$REPO/releases/latest/download/$ASSET"
        fi
        TARBALL="$TMP/$ASSET"
        say "downloading $URL"
        fetch_to "$URL" "$TARBALL" || fail "download failed: $URL"

        if [ "$SKIP_SUM" = 1 ]; then
            note 'checksum not verified (--skip-checksum)'
        else
            fetch_to "$URL.sha256" "$TMP/sum" \
                || fail "cannot fetch $ASSET.sha256 to check the download. Re-run with --skip-checksum to install without checking."
            # Both shapes: a bare hash, and the "<hash>  <file>" line shasum
            # writes -- which is what the release step produces on POSIX.
            _want=$(tr -d '\r' < "$TMP/sum" | grep -o '[0-9A-Fa-f]\{64\}' | head -1 || true)
            [ -n "$_want" ] || fail "$ASSET.sha256 holds no SHA-256"
            _got=$(sha256_of "$TARBALL")
            [ -n "$_got" ] || fail 'no SHA-256 tool found (need shasum, sha256sum or openssl) -- re-run with --skip-checksum to install unverified'
            _want=$(printf '%s' "$_want" | tr '[:upper:]' '[:lower:]')
            _got=$(printf '%s' "$_got"   | tr '[:upper:]' '[:lower:]')
            [ "$_want" = "$_got" ] || fail "checksum mismatch
    expected $_want
    got      $_got"
            note "sha256 ok ($_got)"
        fi
    fi

    # ---- unpack -----------------------------------------------------------
    # Into a staging directory and only then into place, so a truncated
    # download never half-replaces a working install.
    STAGE="$TMP/unpacked"
    mkdir -p "$STAGE"
    have tar || fail 'tar is not installed'
    tar -xzf "$TARBALL" -C "$STAGE" || fail "cannot unpack $TARBALL"

    # The release tarball carries one rakupp/ directory holding bin/ lib/
    # include/; accept a flat archive too, so a re-rolled download installs.
    ROOT=$STAGE
    if [ ! -x "$STAGE/bin/rakupp" ]; then
        ROOT=''
        for _d in "$STAGE"/*; do
            [ -x "$_d/bin/rakupp" ] || continue
            [ -z "$ROOT" ] || fail 'the archive holds more than one bin/rakupp'
            ROOT=$_d
        done
        [ -n "$ROOT" ] || fail 'the archive holds no bin/rakupp'
    fi

    mkdir -p "$PREFIX"
    # The old bin/ goes first, so a command dropped from a later release does
    # not survive an upgrade as a stale name. Contents are copied, not the
    # directory: `cp -R "$ROOT" "$PREFIX"` would nest rakupp/ inside the prefix.
    rm -rf "$BIN"
    for _e in "$ROOT"/* "$ROOT"/.[!.]*; do
        [ -e "$_e" ] || continue
        cp -R "$_e" "$PREFIX/"
    done
    [ -x "$EXE" ] || fail "install failed: no $EXE"
    say "unpacked into $PREFIX"

    # It has to RUN, not merely exist: a truncated download, a cut-down archive
    # and a half-copied runtime all pass a file-exists check.
    VER=$("$EXE" --version 2>/dev/null | head -1 || true)
    [ -n "$VER" ] || fail "$EXE does not run"
    _check=$("$EXE" -e 'print 6 * 7' 2>&1 || true)
    [ "$_check" = 42 ] || fail "$EXE ran but answered [$_check] where 42 was due"
    # The number alone, for the receipt: "Raku++ (rakupp) 3.27.0 }i{ ..."
    VERNUM=$(printf '%s' "$VER" | sed -n 's/.*(rakupp)[[:space:]]*\([0-9][0-9.]*\).*/\1/p')
    [ -n "$VERNUM" ] || VERNUM=$VER

    # Leave a copy of this script in the prefix, when there is one to copy:
    # --uninstall is what a user comes back for. Under `curl | sh` the script
    # is standard input and there is no file, which is why the undo line
    # printed at the end has a piped form too.
    SELF_COPY=''
    case "${0-}" in
        ''|-*|sh|bash|dash|/bin/sh|/bin/bash) ;;
        *)  if [ "$0" = "$PREFIX/install.sh" ]; then
                SELF_COPY=$0
            elif [ -f "$0" ]; then
                if cp "$0" "$PREFIX/install.sh" 2>/dev/null; then
                    chmod +x "$PREFIX/install.sh" 2>/dev/null || true
                    SELF_COPY="$PREFIX/install.sh"
                fi
            fi ;;
    esac

    # ---- the second name --------------------------------------------------
    # rakupp resolves its runtime from its own executable's REAL path, so a
    # symlink named raku inside bin/ is a complete engine and not a stub:
    # `raku --exe prog.raku` compiles exactly as `rakupp --exe` does. The
    # link is relative, so it survives a prefix that later moves.
    OTHER_RAKU=$(other_on_path raku || true)
    _alias=0
    if   [ "$WANT_ALIAS"  = 1 ]; then _alias=1
    elif [ "$WANT_ALIAS"  = 0 ]; then _alias=0
    elif [ "$HAD_ALIAS"   = 1 ]; then _alias=1     # an upgrade keeps the name it had
    elif [ "$INTERACTIVE" = 1 ]; then
        if [ -n "$OTHER_RAKU" ]; then
            say "another raku is already on your PATH: $OTHER_RAKU"
            if ask 'Install rakupp under the name "raku" as well? [y/N]' n; then _alias=1; fi
        else
            if ask 'Install rakupp under the name "raku" as well? [Y/n]' y; then _alias=1; fi
        fi
    else
        note 'nobody to ask about the name "raku"; pass --raku-alias to get it'
    fi

    if [ "$_alias" = 1 ]; then
        # Re-made on every run: an upgrade replaces rakupp with a new file, and
        # a link left over from before could name a path that no longer exists.
        rm -f "$ALIAS"
        ln -s rakupp "$ALIAS" 2>/dev/null || cp "$EXE" "$ALIAS" \
            || fail "could not create $ALIAS"
        _check=$("$ALIAS" -e 'print 6 * 7' 2>&1 || true)
        [ "$_check" = 42 ] || fail "$ALIAS was created but answered [$_check] where 42 was due"
        say 'installed the name raku'
    fi

    # ---- PATH -------------------------------------------------------------
    RC_TOUCHED=''
    PATH_STATE='not changed (--no-path)'
    if [ "$NO_PATH" = 0 ]; then
        write_env_files
        _added=0; _seen=0
        _oldifs=$IFS; IFS='
'
        for _rc in $(rc_files); do
            IFS=$_oldifs
            _seen=1
            if rc_add "$_rc"; then _added=1; fi
            if [ -n "$RC_TOUCHED" ]; then RC_TOUCHED="$RC_TOUCHED
$_rc"; else RC_TOUCHED=$_rc; fi
            IFS='
'
        done
        IFS=$_oldifs
        if   [ "$_seen"  = 0 ]; then PATH_STATE='no shell startup file to write to'
        elif [ "$_added" = 1 ]; then PATH_STATE='added'
        else PATH_STATE='already there'
        fi
    fi

    if [ "$_alias" = 1 ]; then _aliasjson=true; else _aliasjson=false; fi
    write_receipt "$VERNUM" "$ASSET" "$_aliasjson" "$RC_TOUCHED"

    # ---- what happened ----------------------------------------------------
    echo
    say "$VER"
    note "prefix    $PREFIX"
    if [ "$_alias" = 1 ]; then note 'commands  rakupp, raku'; else note 'commands  rakupp'; fi
    note "PATH      $BIN -- $PATH_STATE"
    _oldifs=$IFS; IFS='
'
    for _rc in $RC_TOUCHED; do IFS=$_oldifs; note "          $_rc"; IFS='
'; done
    IFS=$_oldifs
    if [ "$_alias" = 1 ] && [ -n "$OTHER_RAKU" ]; then
        if [ "$PREPEND" = 1 ]; then
            note "          raku here wins over $OTHER_RAKU"
        else
            note "          $OTHER_RAKU still wins over raku here; re-run with --prepend to swap them"
        fi
    fi
    _brew=$(other_on_path rakupp || true)
    if [ -n "$_brew" ] && looks_like_brew "$_brew"; then
        note "note      Homebrew has a rakupp too, at $_brew"
        note '          brew upgrade rakupp updates that one, not this one'
    fi
    note 'update    rakupp upgrade'
    if [ -n "$SELF_COPY" ]; then
        note "undo      sh \"$SELF_COPY\" --uninstall"
    else
        note "undo      curl -fsSL $SITE | sh -s -- --uninstall"
    fi
    note 'open a new terminal for the PATH change, then: rakupp -e "say 6 * 7"'
}

main "$@"
