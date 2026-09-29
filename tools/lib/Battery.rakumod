unit module Battery;

# What the battery instruments (tools/battery-scan.raku, tools/battery-use-smoke.raku)
# share: where the module battery is, what is in it, how one engine process is
# run against it, and how the last release's own binary is fetched to serve as
# the reference.
#
# The battery is the checkout of raku-module-battery: RAKUPP_BATTERY=/path, or
# ~/raku-module-battery. Its dists/ holds the vendored distributions, one
# directory each; harness/battery.sb is the sandbox profile (no network, writes
# confined to a work directory) its own runners use.
#
# Use it as:
#     use lib $?FILE.IO.parent.add('lib').Str;   # from anything in tools/
#     use Battery;

#| The battery checkout, or exit 2 with how to get one. Exit 2 is "could not
#| judge" in every gate here, never a pass.
sub battery-root(Str :$tool! --> IO::Path) is export {
    my $root = (%*ENV<RAKUPP_BATTERY> // ((%*ENV<HOME> // '.') ~ '/raku-module-battery')).IO;
    unless $root.add('dists').d {
        note "$tool: no module battery at $root (it needs a dists/ directory).";
        note "Set RAKUPP_BATTERY=/path/to/raku-module-battery.";
        exit 2;
    }
    $root
}

#| The distribution directories, sorted by name.
sub battery-dists(IO::Path $root --> List) is export {
    $root.add('dists').dir.grep(*.d).sort(*.basename).List
}

#| The `lib/` of every distribution, joined for RAKULIB. Both engines accept a
#| comma, and it is the only separator Rakudo accepts.
sub battery-rakulib(IO::Path $root --> Str) is export {
    battery-dists($root).map(*.add('lib')).grep(*.d).map(*.absolute).join(',')
}

#| Every module file under a distribution's lib/, sorted.
sub lib-files(IO::Path $dist --> List) is export {
    my @out;
    my @todo = $dist.add('lib');
    while @todo {
        my $d = @todo.shift;
        next unless $d.d;
        for $d.dir -> $e {
            if $e.d { @todo.push: $e }
            elsif $e.basename ~~ / '.' [ rakumod | pm6 | pm ] $ / { @out.push: $e }
        }
    }
    @out.sort(*.Str).List
}

#| A JSON document, read by whichever engine is running the tool: rakupp has its
#| own reader and no Rakudo::Internals, Rakudo the reverse.
sub read-json(Str $text) is export {
    (try ::('Rakupp::Internals::JSON').from-json($text))
        // (try Rakudo::Internals::JSON.from-json($text))
}

#| The module names a distribution provides, from its META6.json (or the older
#| META.info), sorted. Empty when there is no readable META.
sub provided-modules(IO::Path $dist --> List) is export {
    my $meta = <META6.json META.info>.map({ $dist.add($_) }).first(*.f);
    return () without $meta;
    my $m = read-json($meta.slurp);
    return () unless $m ~~ Associative && $m<provides> ~~ Associative;
    $m<provides>.keys.sort.List
}

my $SANDBOX-EXEC = '/usr/bin/sandbox-exec';

#| Run one command with a time cap, stdin closed, the given environment, and on
#| macOS inside the battery's sandbox profile when there is one.
#|
#| The cap is `perl -e 'alarm N; exec …'`, not a Promise race: under rakupp an
#| `await Promise.anyof($proc-promise, Promise.in(N))` marks a still-running
#| process's promise Broken, so a race cannot tell a hang from an exit. perl
#| execs in place, so the signal reported is the engine's own, and a hang shows
#| up as SIGALRM (14). The environment goes through `env` for the same kind of
#| reason: rakupp ignores `.start(:ENV)`.
#|
#| Returns a Hash: rc, signal, hung (Bool), ms, out, err.
sub run-capped(@cmd, Int :$cap!, IO::Path :$work!, :%env, IO::Path :$sandbox --> Hash) is export {
    my @full = 'env', |%env.sort(*.key).map({ "{.key}={.value}" });
    @full.append: $SANDBOX-EXEC, '-f', $sandbox.absolute, '-D', "WORK={$work.absolute}"
        if $sandbox && $sandbox.f && $SANDBOX-EXEC.IO.x;
    @full.append: 'perl', '-e', 'alarm shift; exec @ARGV or die "exec: $!"', ~$cap, |@cmd;
    my $p = Proc::Async.new(:w, |@full);
    my ($out, $err) = '', '';
    $p.stdout.tap(-> $c { $out ~= $c });
    $p.stderr.tap(-> $c { $err ~= $c });
    my $t0 = now;
    my $done = $p.start(:cwd($work.absolute));
    $p.close-stdin;
    my $r = await $done;
    my $sig = $r.signal // 0;
    %( rc => $r.exitcode, signal => $sig, hung => $sig == 14,
       ms => ((now - $t0) * 1000).Int, :$out, :$err )
}

#| The first line worth showing from a run: the first non-empty stderr line, else
#| stdout's. Tabs and newlines go, so it fits a TSV cell.
sub first-line(%r, Int :$max = 160 --> Str) is export {
    my $l = (%r<err>.lines.first(*.trim.chars) // %r<out>.lines.first(*.trim.chars) // '').trim;
    $l.subst(/\t/, ' ', :g).substr(0, $max)
}

#| The name of a signal number, for reports.
sub signal-name(Int $n --> Str) is export {
    my %n = 4 => 'SIGILL', 6 => 'SIGABRT', 8 => 'SIGFPE', 9 => 'SIGKILL', 10 => 'SIGBUS',
            11 => 'SIGSEGV', 13 => 'SIGPIPE', 14 => 'SIGALRM (time cap)', 15 => 'SIGTERM';
    %n{$n} // "signal $n"
}

#| The release asset holding a rakupp for this machine, or '' when there is none.
sub release-asset(--> Str) is export {
    return 'rakupp-macos-universal.tar.gz' if $*DISTRO.name ~~ / :i macos | darwin /;
    if $*KERNEL.name ~~ / :i linux / {
        my $u = run('uname', '-m', :out, :err);
        my $m = $u.out.slurp(:close).trim; $u.err.slurp(:close);
        return "rakupp-linux-{$m eq 'arm64' ?? 'aarch64' !! $m}.tar.gz";
    }
    ''
}

#| The newest published release tag, or '' when `gh` cannot say.
sub latest-release(--> Str) is export {
    my $p = run('gh', 'release', 'view', '-R', 'ash/rakupp', '--json', 'tagName', '-q', '.tagName',
                :out, :err);
    my $t = $p.out.slurp(:close).trim; $p.err.slurp(:close);
    $t
}

#| The rakupp binary of a published release, downloaded once into a cache and
#| reused after that. The reference of a regression scan has to be what users
#| of that release actually run, so this is the release's own asset, checked
#| against its published sha256, and never a build of the tag.
#|
#| The cache is RAKUPP_RELEASE_CACHE, or $XDG_CACHE_HOME/rakupp-releases, or
#| ~/.cache/rakupp-releases. Returns the binary's path, or exits 2.
sub release-binary(Str $tag, Str :$tool! --> Str) is export {
    my $asset = release-asset();
    unless $asset {
        note "$tool: no release asset for {$*KERNEL.name}; pass --ref=/path/to/rakupp.";
        exit 2;
    }
    my $base = %*ENV<RAKUPP_RELEASE_CACHE>
            // ((%*ENV<XDG_CACHE_HOME> // ((%*ENV<HOME> // '.') ~ '/.cache')) ~ '/rakupp-releases');
    my $dir = $base.IO.add($tag);
    my $bin = $dir.add('rakupp').add('bin').add('rakupp');
    return $bin.absolute if $bin.x;

    note "$tool: fetching $asset of $tag into $dir";
    mkdir $dir;
    my $dl = run('gh', 'release', 'download', $tag, '-R', 'ash/rakupp', '--clobber',
                 '-p', $asset, '-p', "$asset.sha256", '-D', $dir.absolute, :out, :err);
    my $dl-err = $dl.err.slurp(:close); $dl.out.slurp(:close);
    unless $dl.exitcode == 0 && $dir.add($asset).f {
        note "$tool: could not download $asset of $tag: {$dl-err.trim}";
        exit 2;
    }
    my $want = $dir.add("$asset.sha256").f ?? $dir.add("$asset.sha256").slurp.words[0] !! '';
    my $sum = run('shasum', '-a', '256', $dir.add($asset).absolute, :out, :err);
    my $got = $sum.out.slurp(:close).words[0] // ''; $sum.err.slurp(:close);
    unless $want && $got eq $want {
        note "$tool: $asset of $tag does not match its published sha256 ({$want || 'none published'}, got {$got || 'nothing'}).";
        $dir.add($asset).unlink;
        exit 2;
    }
    my $x = run('tar', '-xzf', $dir.add($asset).absolute, '-C', $dir.absolute, :out, :err);
    $x.out.slurp(:close); my $x-err = $x.err.slurp(:close);
    unless $bin.x {
        note "$tool: $asset of $tag did not unpack to rakupp/bin/rakupp: {$x-err.trim}";
        exit 2;
    }
    $bin.absolute
}
