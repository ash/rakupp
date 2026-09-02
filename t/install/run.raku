# The installer gate (MODULES-PLAN Part A, M1-M4) — network-free: a local
# fixture index and a locally built archive stand in for the fez ecosystem
# (RAKUPP_INSTALL_INDEX accepts a file path, and archive paths in a file
# index resolve beside it). What it pins:
#
#   - resolution and the dry run (M1)
#   - the checksum gate: a corrupted archive is refused (M2)
#   - a real install: fetch, unpack, test, write the CURI store — then the
#     module LOADS from that store (M3)
#   - the test gate: a dist whose own suite fails is refused; --no-test
#     overrides (M4)
#   - install --list, and the additive already-installed answer (M5)
#
# The cross-ENGINE half of the gate (Rakudo loading the rakupp-written
# store) needs a Rakudo and lives in the release procedure, not here.
#
#   build/rakupp t/install/run.raku      (needs curl-less local mode: tar + a sha1 tool)

my $ROOT = $?FILE.IO.parent.parent.parent;
my $EXE  = $*EXECUTABLE.absolute;
my $tmp  = $*TMPDIR.add("install-gate-$*PID");
$tmp.mkdir;
LEAVE { run 'rm', '-rf', $tmp.Str }

my $ok = 0;
my $bad = 0;
sub check(Bool() $cond, $desc) {
    if $cond { $ok++;  say "ok - $desc" }
    else     { $bad++; say "NOT OK - $desc" }
}

sub sha1-of(Str $path) {
    for ('shasum', '-a', '1'), ('sha1sum',), ('openssl', 'sha1', '-r') -> @tool {
        my $p = try run |@tool, $path, :out, :err;
        next unless $p && $p.exitcode == 0;
        return ~$0 if $p.out.slurp(:close) ~~ / ( <[0..9 a..f]> ** 40 ) /;
    }
    die "no sha1 tool";
}

# ---- build a fixture distribution and its archive ---------------------------
# :shared adds a SECOND provided module (Gate::Shared::<name-tail>… no —
# the same Gate::Shared with byte-identical content in every dist that asks),
# so two dists share one content-addressed blob; :depends declares runtime deps.
sub make-dist(Str $name, Str $modname, Str $version,
              Bool :$failing-test, Bool :$shared, :@depends, :@build-depends,
              Bool :$build-hook, Bool :$bin, Bool :$native-lib) {
    my $safe = $name.subst('::', '-', :g);
    my $droot = $tmp.add("build-$safe-$version");
    my $libdir = $droot.add('lib').add($modname.split('::')[0]);
    $libdir.mkdir;
    my $file = "lib/{$modname.split('::').join('/')}.rakumod";
    # :native-lib — the JSON::Native shape: META declares a resource by its
    # LOGICAL name (libraries/gate) and a build hook writes the platform
    # spelling (libgate.dylib / libgate.so / gate.dll). The module reads it
    # back through %?RESOURCES, which applies the same mapping on lookup.
    # Q[…]: %?RESOURCES must reach the written module as TEXT — in a qq
    # string it would interpolate right here, against this test file's own
    # (empty) resources, and the module would ship with the lookup missing.
    my $extra = $native-lib
        ?? Q[sub native-lib() is export { (try %?RESOURCES<libraries/gate>.IO.slurp) // 'MISSING' }]
        !! '';
    $droot.add($file).spurt(qq:to/END/);
        unit module $modname;
        sub which-version() is export \{ '$version' \}
        $extra
        END
    my $provides = "\"$modname\": \"$file\"";
    if $bin {
        $droot.add('bin').mkdir;
        $droot.add('bin/gate-hello').spurt(
            "use $modname;\nprint 'gate-hello ' ~ which-version();\n");
    }
    if $shared {
        $droot.add('lib/Gate').mkdir;
        # BYTE-IDENTICAL in every dist that carries it — one blob, shared
        $droot.add('lib/Gate/Shared.rakumod').spurt(
            "unit module Gate::Shared;\nsub shared-answer() is export \{ 17 \}\n");
        $provides ~= ", \"Gate::Shared\": \"lib/Gate/Shared.rakumod\"";
    }
    my $deps = @depends.map({ "\"$_\"" }).join(", ");
    my $bdeps = @build-depends.map({ "\"$_\"" }).join(", ");
    my $res = $native-lib ?? '"resources": ["libraries/gate"],' !! '';
    $droot.add('META6.json').spurt(qq:to/END/);
        \{ "name": "$name", "version": "$version", "auth": "test:gate",
           "provides": \{ $provides \}, "depends": [$deps], $res
           "build-depends": [$bdeps] \}
        END
    if $native-lib {
        $droot.add('Build.rakumod').spurt(q:to/END/);
            unit class Build;
            method build($cwd) {
                my $ext  = $*DISTRO.is-win ?? 'dll' !! ($*KERNEL.name eq 'darwin' ?? 'dylib' !! 'so');
                my $stem = $*DISTRO.is-win ?? "gate.$ext" !! "libgate.$ext";
                my $dir  = $cwd.IO.add('resources/libraries');
                $dir.mkdir;
                $dir.add($stem).spurt('native bytes');
                True
            }
            END
    }
    if $build-hook {
        # the zef protocol: Build.rakumod at the root, method build($cwd).
        # Importing the build-dep proves the hook child sees the target store;
        # writing built.flag proves the hook ran BEFORE the tests read it.
        # NB not .split(':')[0] — that bisects the '::' of Gate::Demo
        my $use-bdep = @build-depends
            ?? "use {@build-depends[0]};" !! '';
        $droot.add('Build.rakumod').spurt(qq:to/END/);
            $use-bdep
            unit class Build;
            method build(\$cwd) \{ \$cwd.IO.add('built.flag').spurt('ok'); True \}
            END
    }
    $droot.add('t').mkdir;
    $droot.add('t/01-load.rakutest').spurt($build-hook
        ?? ( @depends
             ?? qq[use {@depends[0]}; print "1..1\\n"; print ('built.flag'.IO.e && which-version() eq '0.4.2') ?? "ok 1\\n" !! "not ok 1\\n";]
             !! qq[print "1..1\\n"; print 'built.flag'.IO.e ?? "ok 1\\n" !! "not ok 1\\n";] )
        !! $failing-test
        ?? qq[use $modname; print "1..1\\nnot ok 1\\n"; exit 1;]
        !! qq[use $modname; print "1..1\\n"; print which-version() eq '$version' ?? "ok 1\\n" !! "not ok 1\\n";]);
    my $tar = $tmp.add("$safe-$version.tar.gz").Str;
    my $p = run 'tar', '-czf', $tar, '-C', $tmp.Str, "build-$safe-$version", :err;
    die "tar failed" if $p.exitcode != 0;
    my $sha = sha1-of($tar);
    my $named = $tmp.add("$sha.tar.gz");
    $tar.IO.rename($named);
    ($named.Str, $sha)
}

my ($arc1, $sha1a) = make-dist('Gate::Demo', 'Gate::Demo', '0.4.2', :bin);
my ($arc2, $sha2)  = make-dist('Gate::Flaky', 'Gate::Flaky', '0.1.0', :failing-test);
my ($arc3, $sha3)  = make-dist('Gate::ShareA', 'Gate::ShareA', '0.1.0', :shared);
my ($arc4, $sha4)  = make-dist('Gate::ShareB', 'Gate::ShareB', '0.1.0', :shared);
my ($arc5, $sha5)  = make-dist('Gate::Consumer', 'Gate::Consumer', '0.1.0',
                               :depends(['Gate::Demo']));
# a dependency pinned to an identity NO index entry carries (the JSONL shape:
# JSON::Fast:ver<0.19>:auth<cpan:TIMOTIMO> predates the author's cpan→zef
# migration) — resolution must fall back to the name, loudly
my ($arc6, $sha6)  = make-dist('Gate::Pinned', 'Gate::Pinned', '0.1.0',
                               :depends(['Gate::Demo:ver<0.4.2>:auth<cpan:GONE>']));
# the OpenSSL shape: a Build.rakumod that imports a BUILD-dependency and
# generates a file the test suite then requires — plus a runtime dep the
# suite imports through the target store
my ($arc7, $sha7)  = make-dist('Gate::Built', 'Gate::Built', '1.0',
                               :build-hook, :depends(['Gate::Demo']),
                               :build-depends(['Gate::Demo']));
# the File::Temp shape: META6 writes `depends` as the PHASE hash rather than
# a list — 223 dists in the zef index do, File::Temp 0.0.12 among them — with
# an object-form dependency and a bin-only alternation mixed in, both of which
# resolve to nothing installable and must stay silent.
my ($arc8, $sha8)  = make-dist('Gate::Phased', 'Gate::Phased', '0.1.0');
# …and an alternation between two RAKU dists: nobody's alternative gets picked
# for them, so it is reported instead of guessed at.
my ($arc9, $sha9)  = make-dist('Gate::Choice', 'Gate::Choice', '0.1.0');
# the JSON::Native shape: a logical libraries/ resource whose file the build
# hook writes under the platform's spelling
my ($arc10, $sha10) = make-dist('Gate::Native', 'Gate::Native', '0.1.0', :native-lib);
# two versions of ONE name, plus a third that exists only as a checkout, and a
# dependent pinned to a floor above everything the index carries — the shapes
# behind `uninstall Foo` with two Foos installed, and behind issue #40
my ($arcT1, $shaT1) = make-dist('Gate::Twin', 'Gate::Twin', '1.0');
my ($arcT2, $shaT2) = make-dist('Gate::Twin', 'Gate::Twin', '2.0');
my ($arcT3, $shaT3) = make-dist('Gate::Twin', 'Gate::Twin', '3.0');
my ($arcF, $shaF)   = make-dist('Gate::Floor', 'Gate::Floor', '0.1.0',
                                :depends(['Gate::Twin:ver<3.0+>']));

$tmp.add('index.json').spurt(qq:to/END/);
    [ \{ "name": "Gate::Demo", "version": "0.4.2", "auth": "test:gate",
         "dist": "Gate::Demo:ver<0.4.2>:auth<test:gate>",
         "provides": \{ "Gate::Demo": "lib/Gate/Demo.rakumod" \},
         "depends": [], "path": "$sha1a.tar.gz" \},
      \{ "name": "Gate::Flaky", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::Flaky:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::Flaky": "lib/Gate/Flaky.rakumod" \},
         "depends": [], "path": "$sha2.tar.gz" \},
      \{ "name": "Gate::ShareA", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::ShareA:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::ShareA": "lib/Gate/ShareA.rakumod",
                        "Gate::Shared": "lib/Gate/Shared.rakumod" \},
         "depends": [], "path": "$sha3.tar.gz" \},
      \{ "name": "Gate::ShareB", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::ShareB:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::ShareB": "lib/Gate/ShareB.rakumod",
                        "Gate::Shared": "lib/Gate/Shared.rakumod" \},
         "depends": [], "path": "$sha4.tar.gz" \},
      \{ "name": "Gate::Consumer", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::Consumer:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::Consumer": "lib/Gate/Consumer.rakumod" \},
         "depends": ["Gate::Demo"], "path": "$sha5.tar.gz" \},
      \{ "name": "Gate::Pinned", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::Pinned:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::Pinned": "lib/Gate/Pinned.rakumod" \},
         "depends": ["Gate::Demo:ver<0.4.2>:auth<cpan:GONE>"], "path": "$sha6.tar.gz" \},
      \{ "name": "Gate::Built", "version": "1.0", "auth": "test:gate",
         "dist": "Gate::Built:ver<1.0>:auth<test:gate>",
         "provides": \{ "Gate::Built": "lib/Gate/Built.rakumod" \},
         "depends": ["Gate::Demo"], "build-depends": ["Gate::Demo"],
         "path": "$sha7.tar.gz" \},
      \{ "name": "Gate::Phased", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::Phased:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::Phased": "lib/Gate/Phased.rakumod" \},
         "depends": \{ "runtime": \{ "requires": [
                          "Gate::Demo",
                          \{ "name": "curl", "from": "bin" \},
                          \{ "any": ["elinks:from<bin>", "lynx:from<bin>"] \} ] \},
                       "test": \{ "requires": [] \} \},
         "path": "$sha8.tar.gz" \},
      \{ "name": "Gate::Choice", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::Choice:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::Choice": "lib/Gate/Choice.rakumod" \},
         "depends": \{ "runtime": \{ "requires": [
                          \{ "any": ["Gate::ShareA", "Gate::ShareB"] \} ] \} \},
         "path": "$sha9.tar.gz" \},
      \{ "name": "Gate::Native", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::Native:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::Native": "lib/Gate/Native.rakumod" \},
         "depends": [], "path": "$sha10.tar.gz" \},
      \{ "name": "Gate::Twin", "version": "1.0", "auth": "test:gate",
         "dist": "Gate::Twin:ver<1.0>:auth<test:gate>",
         "provides": \{ "Gate::Twin": "lib/Gate/Twin.rakumod" \},
         "depends": [], "path": "$shaT1.tar.gz" \},
      \{ "name": "Gate::Twin", "version": "2.0", "auth": "test:gate",
         "dist": "Gate::Twin:ver<2.0>:auth<test:gate>",
         "provides": \{ "Gate::Twin": "lib/Gate/Twin.rakumod" \},
         "depends": [], "path": "$shaT2.tar.gz" \},
      \{ "name": "Gate::Floor", "version": "0.1.0", "auth": "test:gate",
         "dist": "Gate::Floor:ver<0.1.0>:auth<test:gate>",
         "provides": \{ "Gate::Floor": "lib/Gate/Floor.rakumod" \},
         "depends": ["Gate::Twin:ver<3.0+>"], "path": "$shaF.tar.gz" \} ]
    END

my $home = $tmp.add('home');
$home.mkdir;
my %env = HOME => $home.Str, RAKUPP_INSTALL_INDEX => $tmp.add('index.json').Str;
sub installer(*@args) {
    # a failing Proc sink-throws; hand back plain data instead
    my $p = run 'env', |%env.map({ "{.key}={.value}" }), $EXE, 'install', |@args, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    { exit => (try $p.exitcode) // 1, out => $out, err => $err }
}

# ---- M1: dry run resolves, writes nothing ----------------------------------
my %dry = installer('--dry-run', 'Gate::Demo');
check %dry<exit> == 0 && %dry<out>.contains('Gate::Demo:ver<0.4.2>'),
      'M1: dry run resolves the identity';
check !$home.add('.raku/dist').e, 'M1: dry run writes nothing';

# ---- M3: the real install, then the module loads from the store ------------
my %inst = installer('Gate::Demo');
check %inst<exit> == 0, 'M3: install succeeds';
check $home.add('.raku/dist').d && $home.add('.raku/sources').d && $home.add('.raku/short').d,
      'M3: the CURI layout exists';
my $use = run 'env', "HOME={$home}", 'RAKULIB=', $EXE, '-e',
              'use Gate::Demo; print which-version()', :out, :err;
check $use.out.slurp(:close) eq '0.4.2', 'M3: the installed module loads and runs';
$use.err.slurp(:close);

# ---- named bin wrappers: install writes them, they dispatch, uninstall
#      removes them (the blob itself lives in resources/, Rakudo's layout) ----
my $wrapper = $home.add('.raku/bin/gate-hello');
check $wrapper.e, 'bin: install writes the named wrapper';
check ?($wrapper.mode +& 0o100), 'bin: ...and it is executable';
check $wrapper.slurp.contains('run-script'), 'bin: ...with the dispatch template';
my $wrun = run 'env', |%env.map({ "{.key}={.value}" }), $EXE, $wrapper.Str, :out, :err;
check $wrun.out.slurp(:close) eq 'gate-hello 0.4.2',
      'bin: the wrapper runs the installed script by name';
$wrun.err.slurp(:close);
# the file's short/ index entry is what Rakudo's run-script resolves a bare
# script name through — its presence is what makes the wrapper cross-engine
check $home.add('.raku/short/72FBF518F4A9F9E67352C850F7087DEFA5D51413').d,
      'bin: the script is indexed in short/ under sha1("bin/gate-hello")';

# ---- repository SPECS: inst# reaches the store, file# is a plain dir --------
# HOME points elsewhere, so the spec alone must find the module
my $elsewhere = $tmp.add('elsewhere');
$elsewhere.mkdir;
my $spec-use = run 'env', "HOME={$elsewhere}", 'RAKULIB=', $EXE, '-e',
    'use lib "inst#' ~ $home.add('.raku') ~ '"; use Gate::Demo; print which-version()',
    :out, :err;
check $spec-use.out.slurp(:close) eq '0.4.2',
      'use lib "inst#<store>" resolves an installed module';
$spec-use.err.slurp(:close);
my $spec-I = run 'env', "HOME={$elsewhere}", 'RAKULIB=', $EXE,
    '-I', 'inst#' ~ $home.add('.raku'), '-e', 'use Gate::Demo; print which-version()',
    :out, :err;
check $spec-I.out.slurp(:close) eq '0.4.2', '-I inst#<store> resolves it too';
$spec-I.err.slurp(:close);
my $file-use = run 'env', "HOME={$elsewhere}", 'RAKULIB=', $EXE, '-e',
    'use lib "file#' ~ $tmp.add('build-Gate-Demo-0.4.2') ~ '"; use Gate::Demo; print which-version()',
    :out, :err;
check $file-use.out.slurp(:close) eq '0.4.2',
      'use lib "file#<dir>" is the explicit plain-directory spelling';
$file-use.err.slurp(:close);

# ---- M5: additive; a re-install says so ------------------------------------
my %again = installer('Gate::Demo');
check %again<exit> == 0 && %again<out>.contains('already installed'),
      'M5: an identical re-install is recognized, not repeated';
# …and recognized from the STORE, before any work: no fetch, no build hook,
# no suite. Without the dist/ pre-check every re-install paid the full
# fetch-build-test cycle and learned nothing until the engine refused it.
check !%again<err>.contains('fetching') && !%again<err>.contains('testing'),
      'M5: …without fetching or testing the archive again';
check %again<out>.contains('(already installed)'),
      'M5: the plan marks what the store already holds';
# -q (issue #50): the same re-run says nothing at all — every line it would
# print is narration, and narration is what -q removes; the exit code and
# the store are the answer. Three spellings: before the module, after it,
# and before the command word itself.
my %quiet = installer('-q', 'Gate::Demo');
check %quiet<exit> == 0 && %quiet<out> eq '' && %quiet<err> eq '',
      'M5: install -q on an all-installed plan prints nothing';
my %quiet2 = installer('Gate::Demo', '--quiet');
check %quiet2<exit> == 0 && %quiet2<out> eq '' && %quiet2<err> eq '',
      'M5: …--quiet after the module name too';
{
    my $p = run 'env', |%env.map({ "{.key}={.value}" }), $EXE, '-q', 'install', 'Gate::Demo', :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    check $p.exitcode == 0 && $out eq '' && $err eq '',
          'M5: …and `rakupp -q install`, before the command word';
}
my %qdry = installer('-q', '--dry-run', 'Gate::Demo');
check %qdry<exit> == 0 && %qdry<out>.contains('(already installed)') && %qdry<out>.contains('dry run'),
      'M5: --dry-run -q still prints the plan — it is the product, not narration';
my %list = installer('--list');
check %list<out>.contains('Gate::Demo:ver<0.4.2>'), 'install --list shows it';
# under the identity: the installer, each module's blob, each bin wrapper
my $demo-block = %list<out>.substr(%list<out>.index('Gate::Demo:ver<0.4.2>'));
$demo-block = $demo-block.substr(0, $_) with $demo-block.index("\nGate::");
check $demo-block.contains('installed by: rakupp'),
      'install --list says rakupp installed it';
check $demo-block.contains($home.add('.raku/sources').absolute) && $demo-block.contains("Gate::Demo "),
      'install --list gives the module file its store path';
check $demo-block.contains('bin/gate-hello') && $demo-block.contains($home.add('.raku/bin/gate-hello').absolute),
      'install --list names the bin wrapper and its path';
my %qlist = installer('-q', '--list');
check %qlist<out>.contains('Gate::Demo:ver<0.4.2>') && !%qlist<out>.contains('installed by'),
      '-q --list keeps the identity lines and drops the detail';

# ---- M4: the test gate refuses a dist whose own suite fails ----------------
my %flaky = installer('Gate::Flaky');
check %flaky<exit> != 0 && %flaky<err>.contains('test suite fails'),
      'M4: a failing test suite refuses the install';
my %forced = installer('--no-test', 'Gate::Flaky');
check %forced<exit> == 0, 'M4: --no-test overrides, loudly chosen';

# ---- the trace log: every run leaves an attachable account ------------------
# The support loop this closes: "install did not work on my machine" arrives
# with ~/.raku/rakupp-install/trace.log attached, which opens with the engine
# build stamp and ends with what actually happened.
my $trace = $home.add('.raku/rakupp-install/trace.log');
check $trace.e, 'trace: the log exists';
my $tlog = $trace.e ?? $trace.slurp !! '';
check $tlog.contains('==== rakupp install trace'), 'trace: runs open with a header';
check $tlog.contains('engine: '), 'trace: the engine build stamp is recorded';
check $tlog.contains('engine install: dist-id '), 'trace: an install records its dist-id';
check $tlog.contains('verify: wrapper') && $tlog.contains('gate-hello'),
      'trace: the bin wrapper write is verified on disk';
check $tlog.contains('test FAILED: '), 'trace: a failing suite names the failing file';
check %flaky<err>.contains('trace: '), 'trace: a failure points at the log';

# ---- the raku name: a machine with none gains one in the store's bin/ -------
# The wrappers' shebang is `#!/usr/bin/env raku` — Rakudo's own template; the
# store is shared, so the template is too. Where nothing answers to that
# name, install links <store>/bin/raku to this engine, so the one PATH entry
# the user already needs (the store's bin/) also resolves the shebangs. A
# machine WITH a raku keeps it. Pinned on a synthetic PATH holding only the
# tools the installer runs, so the checks hold whether or not THIS machine
# has a Rakudo. gzip is on the list because GNU tar execs it for -xzf
# (bsdtar decompresses in-process, which is how its absence passed on macOS).
my $tooldir = $tmp.add('tools-no-raku');
$tooldir.mkdir;
for <tar gzip rm shasum sha1sum openssl ln env> -> $t {
    my $w = run 'sh', '-c', "command -v $t", :out, :err;
    my $path = $w.out.slurp(:close).trim;
    $w.err.slurp(:close);
    run 'ln', '-s', $path, $tooldir.add($t).Str if $path && !$tooldir.add($t).e;
}
my $home4 = $tmp.add('home4');
$home4.mkdir;
my %env4 = HOME => $home4.Str, RAKUPP_INSTALL_INDEX => $tmp.add('index.json').Str,
           PATH => $tooldir.Str;
sub installer4(*@args) {
    my $p = run 'env', |%env4.map({ "{.key}={.value}" }), $EXE, 'install', |@args, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    { exit => (try $p.exitcode) // 1, out => $out, err => $err }
}
my %noraku = installer4('Gate::Demo');
my $rlink = $home4.add('.raku/bin/raku');
check %noraku<exit> == 0 && $rlink.e, 'raku-name: a raku-less machine gains the link';
check %noraku<err>.contains('linked: '), 'raku-name: ...said out loud';
my $bare = run 'env', "HOME={$home4}", "PATH={$home4.add('.raku/bin')}:{$tooldir}",
               'RAKULIB=', 'gate-hello', :out, :err;
check $bare.out.slurp(:close) eq 'gate-hello 0.4.2',
      'raku-name: the command runs by bare name through the link';
$bare.err.slurp(:close);
my %chk4 = installer4('--check');
check %chk4<exit> == 0 && %chk4<out>.contains('0 unreferenced'),
      'raku-name: --check counts the link as infrastructure, not waste';
my %again4 = installer4('--force', 'Gate::Demo');
check %again4<exit> == 0 && !%again4<err>.contains('linked: '),
      'raku-name: an existing link is left alone, silently';
my $suite-has-raku = ?((%*ENV<PATH> // '').split(':').first({ $_ ne '' && .IO.add('raku').e }));
check $suite-has-raku ?? !$home.add('.raku/bin/raku').e !! True,
      'raku-name: a machine that answers to raku keeps its raku';

# ---- path installs: zef's rule — a `.`/`/` argument is a directory ----------
# `rakupp install .` (or any path) installs the dist the directory holds:
# no fetch, no checksum, build hook and test gate unchanged, dependencies
# resolved from the index FIRST. The make-dist build directories from the
# fixtures above are exactly such dists.
my $home5 = $tmp.add('home5');
$home5.mkdir;
my %env5 = HOME => $home5.Str, RAKUPP_INSTALL_INDEX => $tmp.add('index.json').Str;
sub installer5(*@args, :$cwd) {
    my $p = $cwd
        ?? run 'env', |%env5.map({ "{.key}={.value}" }), $EXE, 'install', |@args, :out, :err, :cwd($cwd)
        !! run 'env', |%env5.map({ "{.key}={.value}" }), $EXE, 'install', |@args, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    { exit => (try $p.exitcode) // 1, out => $out, err => $err }
}
my $built-dir = $tmp.add('build-Gate-Built-1.0');
my %dot = installer5('.', :cwd($built-dir));
# the printed path is getcwd-real (/private/var/… on macOS) while $tmp keeps
# the symlinked spelling — match the invariant parts, not the prefix
check %dot<exit> == 0 && %dot<err>.contains('installing Gate::Built')
      && %dot<err>.contains('build-Gate-Built-1.0'),
      'path: `install .` installs the cwd dist from its directory';
check %dot<out>.contains('Gate::Demo'),
      'path: ...with its ecosystem dependency planned from the index';
my %plist = installer5('--list');
check %plist<out>.contains('Gate::Built:ver<1.0>'), 'path: --list shows the dist';
my %pagain = installer5($built-dir.Str);
check %pagain<out>.contains('already installed'),
      'path: a path install answers from the store like any other';
my %pun = installer5('--uninstall', '.', :cwd($built-dir));
check %pun<out>.contains('uninstalled Gate::Built'),
      'path: `uninstall .` acts on the dist the directory names';
# a dependency-free path install needs no index: no override, no network
my $home6 = $tmp.add('home6');
$home6.mkdir;
my $demo-dir = $tmp.add('build-Gate-Demo-0.4.2');
my $poff = run 'env', "HOME={$home6}", $EXE, 'install', $demo-dir.Str, :out, :err;
my $poff-out = $poff.out.slurp(:close);
my $poff-err = $poff.err.slurp(:close);
check (try $poff.exitcode) == 0 && !$poff-err.contains('fetching ecosystem index'),
      'path: a dependency-free path install runs offline — no index fetch';
check $home6.add('.raku/bin/gate-hello').e,
      'path: ...and writes the bin wrapper like any install';

# ---- the build hook, and `rakupp test` --------------------------------------
# Gate::Built is the OpenSSL shape: Build.rakumod imports a build-dep from
# the target store and generates a file its own suite requires. Driven
# through --test-only (what `rakupp test` dispatches to): deps install,
# the target runs its hook + suite and stays OUT of the store.
my %tst = installer('--test-only', 'Gate::Built');
check %tst<exit> == 0
      && %tst<err>.contains('building Gate::Built')
      && %tst<err>.contains('suite green, not installed'),
      'rakupp test: the build hook runs, the suite passes against the store';
my %tst-list = installer('--list');
check !%tst-list<out>.contains('Gate::Built'),
      'rakupp test: the tested dist itself is NOT installed';
my %built = installer('Gate::Built');
check %built<exit> == 0 && %built<err>.contains('installed Gate::Built'),
      '…and a real install of the same hooked dist works';
my %built-gone = installer('--uninstall', 'Gate::Built');
check %built-gone<exit> == 0, '…and uninstalls cleanly before the M6 choreography';

# ---- issue #49: the plan is a topological order, not discovery reversed ----
# `rakupp install File::Directory::Tree OpenSSL` names both, and the second
# one's Build.rakumod imports the first. Breadth-first discovery queued the
# tree module first and OpenSSL second, and reversing that ran OpenSSL's hook
# against a store that did not hold its build-dep yet. Gate::Demo before
# Gate::Built is the same shape; a fresh HOME keeps the hook child honest
# (nothing else on the machine can answer `use Gate::Demo`).
my %ord = installer('--dry-run', 'Gate::Demo', 'Gate::Built');
check %ord<exit> == 0 && %ord<out>.index('Gate::Demo') < %ord<out>.index('Gate::Built'),
      'issue #49: a dependency named BEFORE its dependent still plans first';
my $home49 = $tmp.add('home49');
$home49.mkdir;
my %env49 = HOME => $home49.Str, RAKUPP_INSTALL_INDEX => $tmp.add('index.json').Str;
my $i49 = run 'env', |%env49.map({ "{.key}={.value}" }), $EXE, 'install',
              'Gate::Demo', 'Gate::Built', :out, :err;
$i49.out.slurp(:close);
my $e49 = $i49.err.slurp(:close);
check (try $i49.exitcode) == 0 && $e49.contains('installed Gate::Built')
      && !$e49.contains('BUILD FAILED'),
      'issue #49: …and the hook finds its build-dep in a fresh store';
my %tst-mix = installer('--test-only', '--list');
check %tst-mix<exit> == 2, 'test --list is refused as a mode mix';

# ---- a compiled resource: what the hook BUILT reaches the store -------------
# Gate::Native is the JSON::Native shape: META declares `libraries/gate` by
# its logical name, the hook writes the platform spelling (libgate.dylib /
# libgate.so / gate.dll), and the installer must copy the file the hook
# built. For a while it slurped the logical path instead — which does not
# exist — and stored empty bytes, so every compiled extension quietly ran on
# its fallback. A fresh HOME keeps this out of the M6 choreography's store.
my $home7 = $tmp.add('home7');
$home7.mkdir;
my %envn = HOME => $home7.Str, RAKUPP_INSTALL_INDEX => $tmp.add('index.json').Str;
my $ni = run 'env', |%envn.map({ "{.key}={.value}" }), $EXE, 'install', 'Gate::Native', :out, :err;
$ni.out.slurp(:close);
$ni.err.slurp(:close);
check (try $ni.exitcode) == 0, 'native-lib: a dist with a built libraries/ resource installs';
my $nu = run 'env', "HOME={$home7}", 'RAKULIB=', $EXE, '-e',
    'use Gate::Native; print native-lib()', :out, :err;
check $nu.out.slurp(:close) eq 'native bytes',
      'native-lib: %?RESOURCES<libraries/gate> serves the BUILT file from the store';
$nu.err.slurp(:close);
# ...and the record also carries the platform spelling — the key Rakudo's
# lookup computes before consulting it, so the shared store answers both
my $nrec = $home7.add('.raku/dist').dir.first(*.f).slurp;
check $nrec.contains('resources/libraries/libgate') || $nrec.contains('resources/libraries/gate.dll'),
      'native-lib: the dist record keys the platform spelling too';

# ---- the phase-hash `depends` ----------------------------------------------
my %ph-dry = installer('--dry-run', 'Gate::Phased');
check %ph-dry<exit> == 0
      && %ph-dry<out>.contains('2 distributions')
      && %ph-dry<out>.index('Gate::Demo') < %ph-dry<out>.index('Gate::Phased'),
      'phase-hash depends: runtime/requires resolves, dependency first';
check !%ph-dry<out>.contains('does not resolve') && !%ph-dry<out>.contains('curl')
      && !%ph-dry<err>.contains('alternation'),
      '…and the :from<bin> object and bin-only alternation pass without a word';
my %ph = installer('Gate::Phased');
check %ph<exit> == 0 && %ph<err>.contains('installed Gate::Phased'),
      '…and the dist installs through it';
my $ph-use = run 'env', |%env.map({ "{.key}={.value}" }), $EXE,
                 '-e', 'use Gate::Phased; print which-version()', :out, :err;
check $ph-use.out.slurp(:close) eq '0.1.0', '…and the module loads from the store';
installer('--uninstall', 'Gate::Phased');

my %ch = installer('--dry-run', 'Gate::Choice');
check %ch<exit> == 0 && %ch<out>.contains('1 distribution')
      && %ch<out>.contains('alternation this installer does not choose between'),
      'an alternation between Raku dists is reported, never guessed at';

# ---- reinstall: uninstall + install as one command -------------------------
my %re1 = installer('--reinstall', 'Gate::Demo');
check %re1<exit> == 0 && %re1<out>.contains('uninstalled') && %re1<out>.contains('installed Gate::Demo'),
      'reinstall of an installed dist removes and installs';
my $re-use = run 'env', "HOME={$home}", 'RAKULIB=', $EXE, '-e',
                 'use Gate::Demo; print which-version()', :out, :err;
check $re-use.out.slurp(:close) eq '0.4.2', '…and the module still loads after';
$re-use.err.slurp(:close);
my %re2 = installer('--reinstall', 'Gate::Pinned');
check %re2<exit> == 0 && %re2<err>.contains('not installed: Gate::Pinned — installing fresh'),
      'reinstall of a missing dist installs fresh, with the note';
my %re2gone = installer('--uninstall', 'Gate::Pinned');
check %re2gone<exit> == 0, '…and cleans up so the M6 choreography starts fresh';
my %rechk = installer('--check');
check %rechk<exit> == 0 && %rechk<out>.contains('0 broken'), '--check is clean after the reinstalls';

# ---- bare --refresh is a complete command ----------------------------------
my %refresh = installer('--refresh');
check %refresh<exit> == 0 && %refresh<out>.contains('index refreshed'),
      'bare --refresh refetches the index and stops';

# ---- uninstall --list is a mode mix, not a synonym --------------------------
my %mode-mix = installer('--uninstall', '--list');
check %mode-mix<exit> == 2 && %mode-mix<err>.contains('pick one'),
      'uninstall --list is refused instead of quietly listing';

# ---- a dead :auth/:ver pin falls back to the name, loudly ------------------
my %pinned = installer('Gate::Pinned');
check %pinned<exit> == 0 && %pinned<err>.contains('cpan:GONE') && %pinned<err>.contains('is not in the index'),
      'a dependency pinned to a dead identity resolves by name, with the note';
# …and leaves: Gate::Pinned depends on Gate::Demo, so keeping it installed
# would (rightly) block the M6 chain test's second Gate::Demo uninstall
my %pinned-gone = installer('--uninstall', 'Gate::Pinned');
check %pinned-gone<exit> == 0, '…and uninstalls cleanly so M6 starts fresh';

# ---- M6: the checker, then uninstall as gated destruction ------------------
# (the plan's 4b gate: --check clean BEFORE and AFTER every uninstall)
my %chk1 = installer('--check');
check %chk1<exit> == 0 && %chk1<out>.contains('0 broken'), 'M6: --check is clean after installs';

# shared blobs: two dists carry a byte-identical Gate::Shared
installer('Gate::ShareA');
installer('Gate::ShareB');
my %un1 = installer('--uninstall', 'Gate::ShareA');
check %un1<exit> == 0, 'M6: uninstalling one sharer succeeds';
my $shared-still = run 'env', "HOME={$home}", 'RAKULIB=', $EXE, '-e',
                       'use Gate::Shared; print shared-answer()', :out, :err;
check $shared-still.out.slurp(:close) eq '17',
      'M6: the shared blob survives — the other dist still provides it';
$shared-still.err.slurp(:close);
my %chk2 = installer('--check');
check %chk2<exit> == 0 && %chk2<out>.contains('0 broken'), 'M6: --check is clean after the shared uninstall';

# reverse dependencies refuse; removing the dependent first unblocks
installer('Gate::Consumer');
my %rd = installer('--uninstall', 'Gate::Demo');
check %rd<exit> != 0 && %rd<err>.contains('depended on by'),
      'M6: a dist with installed dependents refuses to go';
# …but REINSTALL of the same dist is not blocked: it comes right back
my %rd-re = installer('--reinstall', 'Gate::Demo');
check %rd-re<exit> == 0 && %rd-re<err>.contains('reinstalling in place'),
      'M6: reinstall of a depended-on dist proceeds, with the note';
installer('--uninstall', 'Gate::Consumer');
my %un2 = installer('--uninstall', 'Gate::Demo');
check %un2<exit> == 0, 'M6: …and goes once the dependent is gone';
my %list2 = installer('--list');
check !%list2<out>.contains('Gate::Demo'), 'M6: --list agrees it is gone';
check !$home.add('.raku/bin/gate-hello').e,
      'bin: uninstall removes the named wrapper';
check !$home.add('.raku/short/72FBF518F4A9F9E67352C850F7087DEFA5D51413').e,
      'bin: ...and the script rel-path index entry';
my %chk3 = installer('--check');
check %chk3<exit> == 0 && %chk3<out>.contains('0 broken'), 'M6: --check is clean after the chain';

# what rakupp install did not install is not rakupp's to delete
my $owned = $home.add('.raku/rakupp-install/owned');
my %unb = installer('--uninstall', 'Gate::ShareB');
check %unb<exit> == 0, 'M6: an owned dist uninstalls fine';
installer('--no-test', 'Gate::Flaky');
# simulate a foreign (zef-installed) dist: erase our provenance for Flaky
$owned.spurt("\n");
my %zlist = installer('--list');
check %zlist<out>.contains('installed by: zef'),
      '--list reports a dist without our provenance as zef-installed';
my %notours = installer('--uninstall', 'Gate::Flaky');
check %notours<exit> != 0 && %notours<err>.contains('not installed by'),
      'M6: a dist without our provenance is refused';
# …but reinstall only WARNS on foreign provenance: it replaces, then owns
my %re-foreign = installer('--reinstall', '--no-test', 'Gate::Flaky');
check %re-foreign<exit> == 0 && %re-foreign<err>.contains('reinstalling anyway'),
      'reinstall of a foreign dist warns, replaces, and takes ownership';
check $owned.lines.grep(*.chars).elems >= 1,
      '…and the replacement is recorded in owned';
my %forced2 = installer('--uninstall', '--force', 'Gate::Flaky');
check %forced2<exit> == 0, 'M6: --force overrides for people who mean it';
my %chk4 = installer('--check');
check %chk4<exit> == 0 && %chk4<out>.contains('0 broken'), 'M6: --check is clean at the end';

# ---- M2: the checksum gate refuses a corrupted archive ---------------------
$arc1.IO.spurt("corrupted!" ~ $arc1.IO.slurp(:bin).decode('latin-1'), :enc('latin-1'));
my $home2 = $tmp.add('home2');
$home2.mkdir;
%env<HOME> = $home2.Str;
my %bad-arc = installer('--no-test', 'Gate::Demo');
check %bad-arc<exit> != 0 && %bad-arc<err>.contains('checksum mismatch'),
      'M2: a corrupted archive is refused by checksum';

# ---- the REA fallback: names the zef index lost -----------------------------
# Two dists that exist ONLY in a local REA-format index (entries carry an
# absolute source-url, no path) — the 48-of-top-200 shape from the battery's
# install sweep. Fallback rules pinned here: OFF when the zef index is
# overridden without an REA source; exact-pin resolution from the archive
# BEFORE any loosening; the resolved module really installs and loads.
my ($arcR, $shaR) = make-dist('Gate::Archived', 'Gate::Archived', '1.2');
my ($arcP, $shaP) = make-dist('Gate::OldPin', 'Gate::OldPin', '0.9');
$tmp.add('rea-meta.json').spurt(qq:to/END/);
    [ \{ "name": "Gate::Archived", "version": "1.2", "auth": "test:rea",
         "dist": "Gate::Archived:ver<1.2>:auth<test:rea>",
         "provides": \{ "Gate::Archived": "lib/Gate/Archived.rakumod" \},
         "depends": [], "source-url": "$arcR" \},
      \{ "name": "Gate::OldPin", "version": "0.9", "auth": "cpan:OLD",
         "dist": "Gate::OldPin:ver<0.9>:auth<cpan:OLD>",
         "provides": \{ "Gate::OldPin": "lib/Gate/OldPin.rakumod" \},
         "depends": [], "source-url": "$arcP" \} ]
    END

# zef index overridden, no REA source given: the fallback stays off
my %rea-off = installer('Gate::Archived');
check %rea-off<exit> != 0 && %rea-off<err>.contains('not in the ecosystem index'),
      'REA: with the zef index overridden and no REA source, the fallback stays off';

my $home3 = $tmp.add('home3');
$home3.mkdir;
my %envR = %env.clone;
%envR<HOME> = $home3.Str;
%envR<RAKUPP_INSTALL_REA_INDEX> = $tmp.add('rea-meta.json').Str;
sub installer-rea(*@args) {
    my $p = run 'env', |%envR.map({ "{.key}={.value}" }), $EXE, 'install', |@args, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    { exit => (try $p.exitcode) // 1, out => $out, err => $err }
}

# a name the zef index does not carry resolves from the archive and installs
my %rea-inst = installer-rea('Gate::Archived');
check %rea-inst<exit> == 0 && %rea-inst<err>.contains('resolved from the REA archive'),
      'REA: a zef-index miss resolves from the archive, loudly';
my $rea-use = run 'env', "HOME={$home3}", 'RAKULIB=', $EXE, '-e',
                  'use Gate::Archived; print which-version()', :out, :err;
check $rea-use.out.slurp(:close) eq '1.2', 'REA: the archived module loads from the store';
$rea-use.err.slurp(:close);

# an exact pin the zef index lost is honoured from the archive — no loosening
my %rea-pin = installer-rea('Gate::OldPin:ver<0.9>:auth<cpan:OLD>');
check %rea-pin<exit> == 0
      && %rea-pin<err>.contains('resolved from the REA archive')
      && !%rea-pin<err>.contains('matches nothing'),
      'REA: a dead exact pin is satisfied exactly from the archive';

# ---- one name, two installed versions: uninstall takes both ----------------
# `zef uninstall` matches every installed distribution against the spec and
# removes each one that matches. Two versions behind a name is the ordinary
# result of an upgrade; refusing it left the name unremovable in practice,
# since the identity a refusal asks to be typed instead is a redirection to
# the shell that would have to pass it through.
my $homeT = $tmp.add('home-twin');
$homeT.mkdir;
my %envT = %env.clone;
%envT<HOME> = $homeT.Str;
sub installer-twin(*@args) {
    my $p = run 'env', |%envT.map({ "{.key}={.value}" }), $EXE, 'install', |@args, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    { exit => (try $p.exitcode) // 1, out => $out, err => $err }
}

my %tw1 = installer-twin('Gate::Twin:ver<1.0>');
my %tw2 = installer-twin('Gate::Twin:ver<2.0>');
check %tw1<exit> == 0 && %tw2<exit> == 0,
      'twin: two versions of one name install side by side';
my %twlist = installer-twin('--list');
check %twlist<out>.contains('Gate::Twin:ver<1.0>') && %twlist<out>.contains('Gate::Twin:ver<2.0>'),
      'twin: …and --list shows both';
my %twre = installer-twin('--reinstall', 'Gate::Twin');
check %twre<exit> == 0
      && %twre<out>.contains('uninstalled Gate::Twin:ver<2.0>')
      && %twre<out>.contains('uninstalled Gate::Twin:ver<1.0>')
      && %twre<out>.contains('installed Gate::Twin:ver<2.0>'),
      'twin: reinstall clears every version behind the name and installs the newest';
my %twrelist = installer-twin('--list');
check %twrelist<out>.contains('Gate::Twin:ver<2.0>')
      && !%twrelist<out>.contains('Gate::Twin:ver<1.0>'),
      'twin: …and only the newest is left';
installer-twin('Gate::Twin:ver<1.0>');
my %twun = installer-twin('--uninstall', 'Gate::Twin');
check %twun<exit> == 0
      && %twun<err>.contains('matches 2 installed distributions')
      && %twun<out>.contains('uninstalled Gate::Twin:ver<2.0>')
      && %twun<out>.contains('uninstalled Gate::Twin:ver<1.0>'),
      'twin: a bare name uninstalls every distribution behind it';
my %twchk = installer-twin('--check');
check %twchk<exit> == 0 && %twchk<out>.contains('0 broken'),
      'twin: …and the store is left consistent';

# ---- a ver<X+> floor is a requirement, not a preference (issue #40) --------
# The index carries Gate::Twin 1.0 and 2.0; 3.0 exists only as a checkout, the
# shape of a dependency installed from a local path before its release. The
# floor must be answered by the installed 3.0 — and when nothing answers it,
# REPORTED, never satisfied by loosening the pin down onto an older release.
my %fl1 = installer-twin($tmp.add('build-Gate-Twin-3.0').Str);
check %fl1<exit> == 0, 'floor: a checkout the index does not carry installs from its path';
my %fl2 = installer-twin('--dry-run', 'Gate::Floor');
check %fl2<exit> == 0
      && %fl2<err>.contains('the installed Gate::Twin:ver<3.0>')
      && !%fl2<out>.contains('Gate::Twin:ver<2.0>'),
      'floor: an installed version satisfies a floor the index cannot';
installer-twin('--uninstall', 'Gate::Twin');
my %fl3 = installer-twin('--dry-run', 'Gate::Floor');
check %fl3<out>.contains('nothing at 3.0+')
      && !%fl3<out>.contains('Gate::Twin:ver<2.0>'),
      'floor: …and with nothing to satisfy it, reported rather than downgraded';

say "install gate: $ok ok, $bad failed";
exit 1 if $bad;
