#!/usr/bin/env raku
# Re-measures the two tables in docs/guide/CACHING.md and prints them as
# markdown, so the page can be re-recorded in one sitting instead of one cell at
# a time. It exists because those tables went 2.4x stale between releases and
# nothing in the tree said how they had been produced — the generator for the
# "bare file, no modules" programs in particular was gone, and a table whose
# input cannot be rebuilt cannot be re-measured, only replaced.
#
#   rakupp tools/bench/precomp-table.raku [RAKUPP] [RUNS=15]
#
# `modules` is measured against an installed XML (any version; the row records
# what it found). Skipped, with a note, when XML is not installed.
#
# The method is the page's own: min of N runs, one process per run, comparing
# RAKUPP_NO_PRECOMP=1 against a warm cache. Run it on an idle machine — the
# figures are wall-clock milliseconds of process lifetime, so anything else on
# the box lands in them. It never touches your real cache or config: both are
# redirected into a temporary directory that is removed at the end.

my $RAKUPP = @*ARGS[0] // ($?FILE.IO.parent.parent.parent.add('build-arm64/rakupp').e
                            ?? $?FILE.IO.parent.parent.parent.add('build-arm64/rakupp').Str
                            !! $?FILE.IO.parent.parent.parent.add('build/rakupp').Str);
my $RUNS   = (@*ARGS[1] // 15).Int;
die "no runnable rakupp at $RAKUPP" unless $RAKUPP.IO.x;

my $work = $*TMPDIR.add("precomp-table-$*PID");
mkdir $work;
END { try { .unlink for $work.dir(:!all).grep(*.f); rmdir $work } }

# ---- the workloads --------------------------------------------------------
# `bare file, no modules`: N lines of a plain `my` declaration. Nothing to
# execute, so what is measured is process startup plus the parse, which is what
# --precomp-files caches.
my @sizes = 50, 200, 1000, 5000, 20000;
for @sizes -> $n {
    $work.add("g$n.raku").spurt: (1..$n).map({ "my \$v$_ = $_ + 1;" }).join("\n") ~ "\n";
}
$work.add('empty.raku').spurt("\n");
$work.add('usexml.raku').spurt("use XML;\n");

# ---- the harness ----------------------------------------------------------
my $store = $work.add('store').Str;
sub env($half, $mode) {
    my %e = %*ENV;
    %e<RAKUPP_PRECOMP_DIR> = $store;
    %e<RAKUPP_CONFIG>      = $work.add('rakupp.config').Str;   # never the real one
    %e<RAKUPP_NO_PRECOMP>:delete;
    %e<RAKUPP_PRECOMP_FILES>:delete;
    %e<RAKUPP_PRECOMP_MODULES>:delete;
    $mode eq 'nocache'
        ?? (%e<RAKUPP_NO_PRECOMP> = '1')
        !! (%e{$half eq 'files' ?? 'RAKUPP_PRECOMP_FILES' !! 'RAKUPP_PRECOMP_MODULES'} = '1');
    %e
}
# Returns the min of $RUNS wall-clock milliseconds. `write` empties the cache
# before every run, so it times the run that WRITES an entry rather than one
# that reads it.
sub best($file, $half, $mode) {
    my %e = env($half, $mode);
    run($RAKUPP, '--precomp-clean', :out, :err, :env(%e));
    run($RAKUPP, ~$file, :out, :err, :env(%e)) if $mode eq 'cached';   # warm
    my @t;
    for ^$RUNS {
        run($RAKUPP, '--precomp-clean', :out, :err, :env(%e)) if $mode eq 'write';
        my $t0 = now;
        my $p = run($RAKUPP, ~$file, :out, :err, :env(%e));
        die "$file ($half/$mode) exited {$p.exitcode}" if $p.exitcode != 0;
        @t.push(((now - $t0) * 1000).Num);
    }
    @t.min
}
sub ms($x) { $x.fmt('%.1f') }

say "# docs/guide/CACHING.md — measured tables";
say "";
say "engine:  ", run($RAKUPP, '--version', :out).out.slurp(:close).lines[0];
say "binary:  $RAKUPP";
say "method:  min of $RUNS runs, one process each";
say "load:    ", qqx{uptime}.trim;
say "";

# ---- --precomp-modules ----------------------------------------------------
my $xml = run($RAKUPP, '-e', 'use XML;', :out, :err);
if $xml.exitcode == 0 {
    my ($off, $on) = best($work.add('usexml.raku'), 'modules', 'nocache'),
                     best($work.add('usexml.raku'), 'modules', 'cached');
    say "**`--precomp-modules`**";
    say "";
    say "| | no cache | cached |";
    say "|---|---:|---:|";
    say "| `use XML` | {ms $off} ms | **{ms $on} ms** |";
    say "";
}
else {
    say "**`--precomp-modules`**: skipped, XML is not installed\n";
}

# ---- --precomp-files ------------------------------------------------------
say "**`--precomp-files`**";
say "";
say "| bare file, no modules | no cache | cached | saved | cost to write |";
say "|---|---:|---:|---:|---:|";
for @sizes -> $n {
    my $f = $work.add("g$n.raku");
    my ($off, $on, $w) = best($f, 'files', 'nocache'), best($f, 'files', 'cached'),
                         best($f, 'files', 'write');
    say "| {$n.fmt('%d')} lines | {ms $off} ms | {ms $on} ms | {ms $off - $on} ms | +{ms $w - $off} ms |";
}
say "";
say "startup floor (an empty program, no cache): {ms best($work.add('empty.raku'), 'files', 'nocache')} ms";
