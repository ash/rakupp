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
              Bool :$failing-test, Bool :$shared, :@depends) {
    my $safe = $name.subst('::', '-', :g);
    my $droot = $tmp.add("build-$safe-$version");
    my $libdir = $droot.add('lib').add($modname.split('::')[0]);
    $libdir.mkdir;
    my $file = "lib/{$modname.split('::').join('/')}.rakumod";
    $droot.add($file).spurt(qq:to/END/);
        unit module $modname;
        sub which-version() is export \{ '$version' \}
        END
    my $provides = "\"$modname\": \"$file\"";
    if $shared {
        $droot.add('lib/Gate').mkdir;
        # BYTE-IDENTICAL in every dist that carries it — one blob, shared
        $droot.add('lib/Gate/Shared.rakumod').spurt(
            "unit module Gate::Shared;\nsub shared-answer() is export \{ 17 \}\n");
        $provides ~= ", \"Gate::Shared\": \"lib/Gate/Shared.rakumod\"";
    }
    my $deps = @depends.map({ "\"$_\"" }).join(", ");
    $droot.add('META6.json').spurt(qq:to/END/);
        \{ "name": "$name", "version": "$version", "auth": "test:gate",
           "provides": \{ $provides \}, "depends": [$deps] \}
        END
    $droot.add('t').mkdir;
    $droot.add('t/01-load.rakutest').spurt($failing-test
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

my ($arc1, $sha1a) = make-dist('Gate::Demo', 'Gate::Demo', '0.4.2');
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
         "depends": ["Gate::Demo:ver<0.4.2>:auth<cpan:GONE>"], "path": "$sha6.tar.gz" \} ]
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

# ---- M5: additive; a re-install says so ------------------------------------
my %again = installer('Gate::Demo');
check %again<exit> == 0 && %again<out>.contains('already installed'),
      'M5: an identical re-install is recognized, not repeated';
my %list = installer('--list');
check %list<out>.contains('Gate::Demo:ver<0.4.2>'), 'install --list shows it';

# ---- M4: the test gate refuses a dist whose own suite fails ----------------
my %flaky = installer('Gate::Flaky');
check %flaky<exit> != 0 && %flaky<err>.contains('test suite fails'),
      'M4: a failing test suite refuses the install';
my %forced = installer('--no-test', 'Gate::Flaky');
check %forced<exit> == 0, 'M4: --no-test overrides, loudly chosen';

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
my %chk3 = installer('--check');
check %chk3<exit> == 0 && %chk3<out>.contains('0 broken'), 'M6: --check is clean after the chain';

# what rakupp install did not install is not rakupp's to delete
my $owned = $home.add('.raku/rakupp-install/owned');
my %unb = installer('--uninstall', 'Gate::ShareB');
check %unb<exit> == 0, 'M6: an owned dist uninstalls fine';
installer('--no-test', 'Gate::Flaky');
# simulate a foreign (zef-installed) dist: erase our provenance for Flaky
$owned.spurt("\n");
my %notours = installer('--uninstall', 'Gate::Flaky');
check %notours<exit> != 0 && %notours<err>.contains('not installed by'),
      'M6: a dist without our provenance is refused';
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

say "install gate: $ok ok, $bad failed";
exit 1 if $bad;
