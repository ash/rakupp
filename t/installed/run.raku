# The installed-binary gate (issue #118): the binary under test is COPIED into
# a prefix laid out like an install — bin/rakupp, lib/, include/ — with no
# rakulib/ anywhere near it, HOME pointed at an empty directory, RAKULIB
# cleared and the working directory outside the checkout. Every earlier
# measurement of DBIish ran from a checkout, where <binary>/../rakulib exists,
# so they could not see that an installed rakupp had no NativeHelpers shims
# and loaded the MoarVM-only originals instead.
#
#   build/rakupp t/installed/run.raku                 the offline core
#   build/rakupp t/installed/run.raku --dbiish        + DBIish's SQLite suite,
#                                                       cloned (needs network
#                                                       and libsqlite3)
#   build/rakupp t/installed/run.raku --dbiish=DIR    + the same, from a checkout
#
# The fixtures are in t/fixtures/installed: a program using all three shims
# (its golden output is what Rakudo prints with the real NativeHelpers::Blob),
# and a DECOY dist of the same names whose modules die when loaded.

sub MAIN(:$dbiish) {
    my $ROOT = $?FILE.IO.parent.parent.parent;
    my $FIX  = $ROOT.add('t/fixtures/installed');
    my $SRC  = $*EXECUTABLE.absolute.IO;
    my $tmp  = $*TMPDIR.add("installed-gate-$*PID");
    $tmp.mkdir;
    LEAVE { run 'rm', '-rf', $tmp.Str }

    my $ok = 0;
    my $bad = 0;
    sub check(Bool() $cond, $desc) {
        if $cond { $ok++;  say "ok - $desc" }
        else     { $bad++; say "NOT OK - $desc" }
    }

    # ---- the stage: an install, not a checkout ------------------------------
    my $prefix = $tmp.add('prefix');
    $prefix.add('bin').mkdir;
    my $EXE = $prefix.add('bin/rakupp');
    run 'cp', $SRC.Str, $EXE.Str;
    # --exe links against the runtime archives: a build tree keeps them beside
    # the binary with the headers in src/, a release in lib/ and include/rakupp/
    my ($rtlib, $rtinc) = $SRC.parent.add('librakupp_rt.a').e
        ?? ($SRC.parent, $ROOT.add('src'))
        !! $SRC.parent.parent.add('lib/librakupp_rt.a').e
        ?? ($SRC.parent.parent.add('lib'), $SRC.parent.parent.add('include/rakupp'))
        !! (Nil, Nil);
    my $runtime = $rtlib.defined;
    if $runtime {
        run 'ln', '-s', $rtlib.Str, $prefix.add('lib').Str;
        $prefix.add('include').mkdir;
        run 'ln', '-s', $rtinc.Str, $prefix.add('include/rakupp').Str;
    }
    my $home = $tmp.add('home');
    $home.mkdir;
    my $work = $tmp.add('work');
    $work.mkdir;
    check !$prefix.add('rakulib').e && !$prefix.add('libexec').e && !$work.add('rakulib').e,
          'the stage has no rakulib/ beside the binary or in the working directory';

    # every run: the staged binary, an empty HOME, no RAKULIB, outside the checkout
    sub staged(*@args, :$cwd = $work, *%env) {
        my @env = "HOME=$home", 'RAKULIB=', |%env.map({ "{.key}={.value}" });
        run 'env', |@env, $EXE.Str, |@args, :$cwd, :out, :err
    }

    my $decoy = $FIX.add('decoy-dist');
    my $golden = $FIX.add('uses-shims.out').slurp;

    # ---- the decoy is armed: loaded by FILE, it dies ------------------------
    # (otherwise "the decoy was not loaded" below would pass for any reason)
    my $armed = staged '-e', "EVALFILE '{$decoy.add('lib/NativeHelpers/Blob.rakumod')}'";
    check $armed.exitcode != 0 && $armed.err.slurp(:close).contains('DECOY NativeHelpers::Blob loaded'),
          'the decoy NativeHelpers::Blob dies when loaded';

    # ---- the shims answer from the binary, ahead of -I ----------------------
    my $r = staged '-I', $decoy.add('lib').Str, $FIX.add('uses-shims.raku').Str;
    my $out = $r.out.slurp(:close);
    my $err = $r.err.slurp(:close);
    check $r.exitcode == 0 && $out eq $golden,
          'NativeHelpers::{Blob,CStruct,Pointer} give what Rakudo gives with the real dist';
    check !$err.contains('DECOY'), '…and the decoy on -I is never loaded';
    check $err eq '', '…with nothing on stderr (no NativeLibs-style warning)';
    note $err.indent(4) if $err;

    my $t = staged '-I', $decoy.add('lib').Str, $FIX.add('uses-shims.raku').Str, :RAKUPP_TRACE<1>;
    $t.out.slurp(:close);
    my $trace = $t.err.slurp(:close);
    for <Blob CStruct Pointer> -> $m {
        check $trace.contains("NativeHelpers::$m <- the shadow compiled into this binary"),
              "RAKUPP_TRACE: NativeHelpers::$m came from the binary";
    }

    # ---- the installer does not fetch what the binary carries ---------------
    my $store = $tmp.add('store');
    my $dry = staged 'install', '--dry-run', "--to=$store", $decoy.Str;
    my $dryout = $dry.out.slurp(:close) ~ $dry.err.slurp(:close);
    check $dry.exitcode == 0 && $dryout.contains('(provided by rakupp, not fetched)'),
          'install --dry-run marks NativeHelpers::Blob as provided by rakupp';
    my $ins = staged 'install', "--to=$store", $decoy.Str;
    my $insout = $ins.out.slurp(:close) ~ $ins.err.slurp(:close);
    check $ins.exitcode == 0 && $insout.contains('provided by rakupp'),
          'install of a dist named NativeHelpers::Blob succeeds by skipping it';
    check !$store.add('dist').d || !$store.add('dist').dir.elems,
          '…and writes nothing into the store';
    note $insout.indent(4) if $ins.exitcode;

    # ---- --exe carries the shim into the compiled program -------------------
    if $runtime {
        my $bin = $tmp.add('uses-shims');
        my $c = staged '--exe', '-I', $decoy.add('lib').Str, $FIX.add('uses-shims.raku').Str, '-o', $bin.Str;
        $c.out.slurp(:close);
        my $cerr = $c.err.slurp(:close);
        check $c.exitcode == 0 && $cerr.contains('NativeHelpers::Blob'),
              '--exe from the stage embeds NativeHelpers::Blob';
        my $elsewhere = $tmp.add('elsewhere');
        $elsewhere.mkdir;
        my $rb = run 'env', "HOME=$home", 'RAKULIB=', $bin.Str, :cwd($elsewhere), :out, :err;
        check $rb.exitcode == 0 && $rb.out.slurp(:close) eq $golden,
              '…and the compiled program runs from another directory';
        note $cerr.indent(4) if $c.exitcode;
    }
    else {
        say "# --exe checks skipped: no librakupp_rt.a beside {$SRC}";
    }

    # ---- opt-in: the real DBIish, installed and run as a user would ---------
    if $dbiish {
        my $dir = $dbiish ~~ Str ?? $dbiish.IO.absolute.IO !! $tmp.add('DBIish');
        my $have = $dbiish ~~ Str
            || run('git', 'clone', '-q', '--depth', '1',
                   'https://github.com/raku-community-modules/DBIish.git', $dir.Str).exitcode == 0;
        if !$have {
            say "# DBIish checks skipped: the clone failed (offline?)";
        }
        else {
            my $di = staged 'install', '--no-test', $dir.Str;
            my $diout = $di.out.slurp(:close) ~ $di.err.slurp(:close);
            check $di.exitcode == 0 && $diout.contains('provided by rakupp: NativeHelpers::Blob'),
                  'DBIish installs into an empty HOME without fetching NativeHelpers::Blob';
            note $diout.indent(4) if $di.exitcode;
            my $dt = staged 't/45-sqlite-common.rakutest', :cwd($dir), :DBIISH_WRITE_TEST<YES>;
            my $dtout = $dt.out.slurp(:close);
            my $dterr = $dt.err.slurp(:close);
            my @lines  = $dtout.lines;
            my $passed = @lines.grep({ .starts-with('ok ') && !.contains('# SKIP') }).elems;
            my $skips  = @lines.grep(*.contains('# SKIP')).elems;
            my @failed = @lines.grep({ .starts-with('not ok') && !.contains('# TODO') });
            # 109 planned: three SKIPs are the driver's own (no server version;
            # bind_param_array/execute_array, two tests) and one is a TODO that
            # Rakudo fails too. A missing libsqlite3 turns the whole file into
            # SKIPs that still print `ok` — hence the counts.
            check $dt.exitcode == 0 && !@failed && $passed >= 100 && $skips <= 3,
                  "DBIish t/45-sqlite-common: $passed passed, $skips skipped, {+@failed} failed";
            check !$dterr.contains('Offset') && !$dterr.contains('uninitialized'),
                  '…with no MoarVM-guts failure and no uninitialized-value warning';
            note $dterr.indent(4) if $dterr;
            .indent(4).note for @failed;
        }
    }
    else {
        say "# DBIish checks not run (pass --dbiish, or --dbiish=DIR for a checkout)";
    }

    say "installed-binary gate: $ok ok, $bad failed";
    exit 1 if $bad;
}
