#!/usr/bin/env raku
# `rakupp upgrade` — replace this engine with a newer release.
#
# A Raku program carried INSIDE the binary and dispatched by it, exactly as
# tools/install.raku (`rakupp install`) and tools/doc.raku (`rakupp doc`) are.
# That is what makes it work on Windows with no PowerShell script on disk, and
# what keeps an updater from ever drifting from the engine it updates.
#
# It updates an install that tools/install.sh or tools/install-windows.ps1
# made, and REFUSES every other prefix: a Homebrew, Nix, Guix or distribution
# rakupp is that package manager's to replace, and a build tree is git's.
# The receipt those two installers leave, <prefix>/rakupp-install.json, is how
# this tool tells one from the other — and its absence is not a thing to work
# around, it is the answer.
#
# Transport is `curl` in a subprocess, as install.raku's is: Windows has
# shipped curl.exe since 1803, and the alternative is an HTTPS client that
# would have to work before the engine carrying it does. The checksum is the
# ENGINE's own SHA-256, so nothing beyond curl and tar has to exist on the box
# — which matters on Windows, where install.raku's SHA-1 step has to go
# looking for shasum, sha1sum or openssl and finds none of them.

use Digest::SHA256::Native;

my constant REPO    = 'ash/rakupp';
my constant RECEIPT = 'rakupp-install.json';

# Every archive a release publishes. The receipt names one of these, and an
# entry that is NOT one of them -- the receipt an `--archive` install leaves
# names a local file -- means this platform's own name is the only sensible
# thing to fetch. Keep in step with the matrix in .github/workflows/release.yml.
my constant ASSETS = <
    rakupp-macos-universal.tar.gz
    rakupp-linux-x86_64.tar.gz
    rakupp-linux-aarch64.tar.gz
    rakupp-openbsd-x86_64.tar.gz
    rakupp-windows-x64.zip
    rakupp-windows-x64-mingw.zip
>;

my Bool $QUIET = False;
sub step($m) { say "==> $m" unless $QUIET }
# An empty note is a blank line, not four spaces of trailing whitespace.
sub note($m) { say($m ?? "    $m" !! '') unless $QUIET }
# Failures go to stderr and ignore --quiet: -q is for the noise of success.
sub oops($m) { $*ERR.say($m) }

# ---------------------------------------------------------------------------
# Versions. Compared as numbers per component, never as strings: 3.9.0 is older
# than 3.27.0 and sorts after it (docs/dev/plans/VERSIONS.md).
# ---------------------------------------------------------------------------
sub vparts(Str $v --> List) { $v.subst(/^ 'v' /, '').comb(/\d+/)».Int.List }

sub newer(Str $a, Str $b --> Bool) {
    my @a = vparts($a);
    my @b = vparts($b);
    for ^max(@a.elems, @b.elems) -> $i {
        my $x = @a[$i] // 0;
        my $y = @b[$i] // 0;
        return True  if $x > $y;
        return False if $x < $y;
    }
    return False;
}

# ---------------------------------------------------------------------------
# Where this binary lives. $*EXECUTABLE is the REAL path — resolved through a
# `raku` symlink — so an install whose second name was used still finds its own
# prefix rather than wherever the link sat.
# ---------------------------------------------------------------------------
sub exe-path(--> IO::Path) { $*EXECUTABLE.IO.resolve }
sub prefix-of(IO::Path $exe --> IO::Path) { $exe.parent.parent }

sub read-receipt(IO::Path $prefix) {
    my $f = $prefix.add(RECEIPT);
    return Nil unless $f.e;
    my $text = try $f.slurp;
    return Nil unless $text;
    # The engine's own JSON, as install.raku reads the zef index with; the
    # Rakudo spelling is the fallback that keeps this file runnable under
    # Rakudo too.
    #
    # An explicit `unless` BLOCK, not `$j = try ... unless $j`: `try` is a
    # statement PREFIX, so the modifier binds inside it -- the expression
    # becomes `try ($j ? nothing : from-json(...))`, the skipped statement
    # yields an empty Slip, and the assignment stores that Slip over the hash
    # just parsed. Rakudo does exactly the same thing; it is the language, not
    # a divergence.
    my $j = try ::('Rakupp::Internals::JSON').from-json($text);
    unless $j {
        $j = try Rakudo::Internals::JSON.from-json($text);
    }
    return $j ~~ Associative ?? $j !! Nil;
}

# ---------------------------------------------------------------------------
# Whose rakupp is this? Every branch here ends in the command that DOES update
# the install in front of us — a refusal that only says no is a worse answer
# than the package manager's own line.
# ---------------------------------------------------------------------------
sub foreign-advice(IO::Path $prefix, IO::Path $exe) {
    my $p = $prefix.absolute;

    return "Nix installed this one.\n    nix profile upgrade rakupp"
        if $p.starts-with('/nix/store');

    return "Guix installed this one.\n    guix upgrade rakupp"
        if $p.starts-with('/gnu/store');

    return "Homebrew installed this one.\n    brew upgrade rakupp"
        if $p.contains('/Cellar/') || $p.contains('/homebrew/') || $p.contains('/linuxbrew/');

    # A build tree, not an install: the binary sits beside its own CMake cache.
    for $exe.parent, $exe.parent.parent -> $d {
        return "This is a build tree, not an install.\n    git pull && cmake --build {$d.basename}"
            if $d.add('CMakeCache.txt').e;
    }

    return "A distribution package installed this one; update it the way you\n    installed it (apt, dnf, pacman, pkg …)."
        if $p eq '/usr' || $p.starts-with('/usr/lib') || $p eq '/usr/local' && !$p.contains('homebrew');

    return Nil;
}

# ---------------------------------------------------------------------------
# The latest release, WITHOUT api.github.com: /releases/latest redirects to
# /releases/tag/<tag>, and reading where it landed costs no rate limit at all.
# An unauthenticated API call gets 60 an hour per address, which a shared
# office or a CI runner can and does exhaust.
# ---------------------------------------------------------------------------
sub latest-tag(--> Str) {
    my $p = run 'curl', '-fsS', '--max-time', '60', '-o',
                ($*DISTRO.is-win ?? 'NUL' !! '/dev/null'),
                '-w', '%{url_effective}', '-L',
                "https://github.com/{REPO}/releases/latest", :out, :err;
    my $url = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close);
    die "cannot reach GitHub to ask for the latest release: {$err.trim || "curl exit {$p.exitcode}"}"
        unless $p.exitcode == 0 && $url;
    $url ~~ / 'releases/tag/' (<[\w.\-+]>+) $ /
        or die "GitHub answered with $url, which names no release tag";
    return ~$0;
}

sub asset-for-platform(--> Str) {
    given $*KERNEL.name {
        when 'darwin'  { return 'rakupp-macos-universal.tar.gz' }
        when 'win32'   { return 'rakupp-windows-x64.zip' }
        when 'linux'   {
            return $*KERNEL.hardware ~~ /^ [aarch64 | arm64] $/
                ?? 'rakupp-linux-aarch64.tar.gz'
                !! 'rakupp-linux-x86_64.tar.gz';
        }
        when 'openbsd' { return 'rakupp-openbsd-x86_64.tar.gz' }
    }
    return $*DISTRO.is-win ?? 'rakupp-windows-x64.zip' !! Str;
}

# ---------------------------------------------------------------------------
sub fetch(Str $url, IO::Path $to --> Nil) {
    my $p = run 'curl', '-fsSL', '--proto', '=https', '--max-time', '900',
                '-o', $to.absolute, $url, :out, :err;
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    die "download failed: $url\n    {$err.trim}" unless $p.exitcode == 0 && $to.e;
}

sub unpack(IO::Path $archive, IO::Path $into --> Nil) {
    $into.mkdir;
    # bsdtar reads a zip as happily as a tarball, and Windows has shipped it as
    # tar.exe since 1803 — so one command covers both assets. -z only for the
    # tarball: a GNU tar old enough to need the flag is also one that will not
    # be handed a zip.
    my @cmd = $archive.extension eq 'zip'
        ?? ('tar', '-xf', $archive.absolute, '-C', $into.absolute)
        !! ('tar', '-xzf', $archive.absolute, '-C', $into.absolute);
    my $p = run |@cmd, :out, :err;
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    die "cannot unpack {$archive.basename}: {$err.trim}" unless $p.exitcode == 0;
}

# The directory inside the staging tree that holds bin/, lib/, include/. The
# POSIX tarball wraps them in rakupp/; the Windows zip does not.
sub payload-root(IO::Path $stage, Str $exe-name --> IO::Path) {
    return $stage if $stage.add('bin').add($exe-name).e;
    my @found = $stage.dir.grep({ .d && .add('bin').add($exe-name).e });
    die 'the archive holds no bin/' ~ $exe-name unless @found == 1;
    return @found[0];
}

# ---------------------------------------------------------------------------
# The swap. Entry by entry, old aside and new in, so a failure at any point can
# put every piece back. The running binary is never written THROUGH: it is
# renamed away and a new file takes its name, which POSIX allows outright (the
# process keeps its now-unlinked inode) and Windows allows for a rename though
# not for a delete.
# ---------------------------------------------------------------------------
# One rename, with its result actually LOOKED AT.
#
# `rename($a, $b);` on its own line throws X::IO::Rename on Rakudo and is
# SILENT on rakupp — the Failure it returns is never sunk hard enough to blow
# up, so the statement does nothing and says nothing. A swap built on the
# unchecked spelling would report success having moved not one file. Testing
# the returned value is the one spelling that is correct on both engines: both
# answer False when it is tested.
sub move(IO::Path $from, IO::Path $to --> Bool) {
    my $ok = False;
    try {
        $ok = ?rename($from, $to);
        CATCH { default { $ok = False } }
    }
    return $ok;
}

sub swap(IO::Path $prefix, IO::Path $root --> IO::Path) {
    my $aside = $prefix.add('.rakupp-old-' ~ $*PID);
    $aside.mkdir;
    my @names = $root.dir.map(*.basename).sort;

    # Two passes, not one interleaved: everything the new payload will replace
    # moves out first, then everything new moves in. That is what makes the
    # unwind exact -- two lists, each undone in reverse -- where one pass leaves
    # the entry that failed half way in neither list.
    my @stashed;    # moved OUT of the prefix, into $aside
    my @placed;     # moved INTO the prefix, from $root
    my $failed = '';

    for @names -> $n {
        my $live = $prefix.add($n);
        next unless $live.e || $live.l;      # .e is False for a DANGLING link
        if move($live, $aside.add($n)) { @stashed.push($n) }
        else { $failed = "could not move {$live.absolute} aside"; last }
    }
    unless $failed {
        for @names -> $n {
            if move($root.add($n), $prefix.add($n)) { @placed.push($n) }
            else { $failed = "could not put {$prefix.add($n).absolute} in place"; last }
        }
    }

    if $failed {
        # Newest first, both lists: the prefix goes back to exactly what it was,
        # and the staging tree back to a payload that can simply be discarded.
        move($prefix.add($_), $root.add($_))  for @placed.reverse;
        move($aside.add($_), $prefix.add($_)) for @stashed.reverse;
        die "$failed; the install was left as it was";
    }
    return $aside;
}

# Any .rakupp-old-* a previous run could not delete. On Windows the running
# .exe cannot be REMOVED, only renamed, so the directory a swap moved it into
# survives until the next run — which is this. Best effort by design: one that
# still will not go is left for the run after that, and nothing depends on it
# having gone.
sub sweep-old(IO::Path $prefix --> Nil) {
    for $prefix.dir.grep({ .d && .basename.starts-with('.rakupp-old-') }) -> $d {
        try rmtree($d);
    }
}

sub rmtree($d --> Nil) {
    # Undefined on purpose sometimes: the LEAVE phaser in MAIN fires for the
    # early exits too, before the staging directory it names has been assigned.
    return unless $d.defined && $d.IO.e;
    for $d.dir -> $e {
        if $e.d && !$e.l { rmtree($e) }
        else { try $e.unlink }
    }
    try rmdir($d);
}

# ---------------------------------------------------------------------------
sub rewrite-receipt(IO::Path $prefix, $r, Str $version, Str $asset --> Nil) {
    my %out = $r.hash;
    %out<version> = $version;
    %out<asset>   = $asset;
    %out<upgraded_at> = DateTime.now(:timezone(0)).truncated-to('second').Str;
    my $json = try ::('Rakupp::Internals::JSON').to-json(%out);
    without $json {          # a block, for the reason read-receipt explains
        $json = try Rakudo::Internals::JSON.to-json(%out);
    }
    $prefix.add(RECEIPT).spurt($json ~ "\n") if $json;
}

# ---------------------------------------------------------------------------
sub MAIN(
    Str  :$version,           #= install this tag instead of the latest
    Bool :$check    = False,  #= report what is available and install nothing
    Bool :$force    = False,  #= install even when it is not newer
    Bool :$quiet    = False,
    Bool :$yes      = False,
) {
    # Every failure below is a sentence for the person who typed the command,
    # not a bug report: a backtrace through a tool they cannot edit, and did
    # not know was Raku, tells them nothing they can act on. The message is
    # what matters, and exit 1 is what a script reads.
    CATCH {
        default {
            oops("rakupp upgrade: {.message}");
            exit 1;
        }
    }
    $QUIET = $quiet;

    my $exe     = exe-path();
    my $prefix  = prefix-of($exe);
    my $current = $*RAKU.compiler.id;

    # ---- may we? ----------------------------------------------------------
    my $receipt = read-receipt($prefix);
    without $receipt {
        my $advice = foreign-advice($prefix, $exe)
            // ("This rakupp was not installed by the rakupp installer, so there is\n"
              ~ "    no record of how to replace it. The one-liner sets up an install\n"
              ~ "    that `rakupp upgrade` can keep current:\n"
              ~ "    curl -fsSL https://raku.online/install.sh | sh");
        note "rakupp $current at {$prefix.absolute}";
        note '';
        note $advice;
        exit 1;
    }

    unless $prefix.w {
        note "rakupp $current at {$prefix.absolute}";
        note '';
        note 'That prefix is not writable by you. Re-run as its owner, or install';
        note 'a copy of your own with:  curl -fsSL https://raku.online/install.sh | sh';
        exit 1;
    }

    # ---- what is there? ---------------------------------------------------
    my $tag    = $version ?? ($version.starts-with('v') ?? $version !! "v$version") !! latest-tag();
    my $target = $tag.subst(/^ 'v' /, '');

    if $check {
        step "rakupp $current at {$prefix.absolute}";
        if newer($target, $current) {
            note "$target is available — run `rakupp upgrade` to install it";
            exit 0;
        }
        note $target eq $current
            ?? 'that is the latest release'
            !! "the latest release is $target";
        exit 0;
    }

    unless $force || newer($target, $current) || $version {
        step "rakupp $current is already the latest release";
        exit 0;
    }
    if !$force && $version && $target eq $current {
        step "rakupp $current is already installed (--force reinstalls it)";
        exit 0;
    }

    # The receipt first -- it is the only thing that knows an MSVC install from
    # a MinGW one -- but only when it names an archive a release actually
    # publishes. An `--archive` install recorded a local file name.
    my $asset = $receipt<asset>;
    $asset = asset-for-platform() unless $asset && ASSETS.first($asset);
    die "no release archive for {$*KERNEL.name}/{$*KERNEL.hardware} — build from source instead"
        without $asset;

    my $down = $version ?? "download/$tag" !! "latest/download";
    my $url  = "https://github.com/{REPO}/releases/$down/$asset";

    sweep-old($prefix);

    my $stage = $prefix.add('.rakupp-upgrade');
    rmtree($stage);
    $stage.mkdir;
    LEAVE rmtree($stage);

    step "downloading $url";
    my $archive = $stage.add($asset);
    fetch($url, $archive);

    # The engine's own SHA-256 — no shasum, no sha256sum, no certutil, and so
    # nothing that can be missing on the machine being upgraded.
    fetch("$url.sha256", $stage.add("$asset.sha256"));
    my $sums = $stage.add("$asset.sha256").slurp;
    $sums ~~ /(<[0..9a..fA..F]> ** 64)/ or die "$asset.sha256 holds no SHA-256";
    my $want = (~$0).lc;
    my $got  = sha256-hex($archive.slurp(:bin)).lc;
    die "checksum mismatch\n    expected $want\n    got      $got" unless $want eq $got;
    note "sha256 ok ($got)";

    my $un = $stage.add('unpacked');
    unpack($archive, $un);
    my $exe-name = $exe.basename;
    my $root = payload-root($un, $exe-name);

    # The second name, before the swap: an upgrade keeps what the install had.
    my $want-alias = ?($receipt<raku_symlink> // False);

    my $aside = swap($prefix, $root);

    # It has to RUN. A verified archive can still be the wrong architecture, and
    # a prefix that was replaced with something that does not start is the one
    # failure this tool must not walk away from.
    my $new = $prefix.add('bin').add($exe-name);
    my $ok = False;
    my $answer = '';
    if $new.e {
        my $p = run $new.absolute, '-e', 'print 6 * 7', :out, :err;
        $answer = $p.out.slurp(:close).trim;
        $p.err.slurp(:close);
        $ok = $p.exitcode == 0 && $answer eq '42';
    }
    unless $ok {
        # Put the previous install back. Every entry the swap moved aside is in
        # $aside; what is standing in the prefix now is the payload that does
        # not run, and it goes first or the rename has nowhere to land.
        for $aside.dir -> $old {
            my $live = $prefix.add($old.basename);
            if $live.d && !$live.l { rmtree($live) } else { try $live.unlink }
            move($old, $live);
        }
        rmtree($aside);   # emptied by the loop above; do not leave it to be swept
        die "the new binary did not run (it answered [$answer]); the previous one is back in place";
    }

    if $want-alias {
        my $bin   = $prefix.add('bin');
        my $alias = $bin.add($*DISTRO.is-win ?? 'raku.exe' !! 'raku');
        try $alias.unlink if $alias.e || $alias.l;   # .e is False for a DANGLING link

        # `ln -s` run INSIDE bin/, not the symlink builtin: Raku's `symlink`
        # resolves a relative target against the CWD and stores the result, so
        # symlink('rakupp', …/bin/raku) writes a link to <cwd>/rakupp -- which
        # is not this prefix, and breaks the moment the prefix moves. `ln`
        # stores its target verbatim, which is what makes the link relative and
        # the prefix relocatable. tools/install.sh uses `ln` for the same
        # reason. (Rakudo's `symlink` resolves identically; this is the
        # language, not a divergence.)
        my $made = False;
        unless $*DISTRO.is-win {
            my $p = try run 'ln', '-s', $exe-name, $alias.basename,
                            :cwd($bin.absolute), :out, :err;
            if $p { $p.out.slurp(:close); $p.err.slurp(:close) }
            $made = so ($p && $p.exitcode == 0 && $alias.l);
        }
        # A copy where a link is refused -- Windows without Developer Mode, and
        # any filesystem that has no symlinks. `cp -p` because a plain copy can
        # drop the executable bit, and a raku that cannot be run is worse than
        # no raku at all.
        unless $made {
            my $p = try run 'cp', '-p', $new.absolute, $alias.absolute, :out, :err;
            if $p { $p.out.slurp(:close); $p.err.slurp(:close) }
            $made = so ($p && $p.exitcode == 0 && $alias.e);
        }
        # It has to RUN, like the engine itself did: a link to the wrong place
        # and a copy without its executable bit both pass a file-exists check.
        if $made {
            my $p = run $alias.absolute, '-e', 'print 6 * 7', :out, :err;
            my $said = $p.out.slurp(:close).trim;
            $p.err.slurp(:close);
            note "the name raku was remade but answered [$said]; rakupp itself is fine"
                unless $p.exitcode == 0 && $said eq '42';
        }
        else {
            note 'could not remake the name raku; rakupp itself is installed';
        }
    }

    rewrite-receipt($prefix, $receipt, $target, $asset);
    sweep-old($prefix);

    my $vp = run $new.absolute, '--version', :out, :err;
    my $banner = $vp.out.slurp(:close).lines[0] // '';
    $vp.err.slurp(:close);

    step "$current -> $target";
    note $banner if $banner;
    note "prefix    {$prefix.absolute}";
    note 'commands  ' ~ ($want-alias ?? 'rakupp, raku' !! 'rakupp');
}
