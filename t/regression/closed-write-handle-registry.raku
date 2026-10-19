# Regression: every `open :w` pushed a strong reference onto the exit-flush
# registry (`openWriteHandles_`, src/Interpreter.h) and only program exit
# cleared it, so a program that opened and closed many files grew by about
# 2.2 KB per closed handle: 91 MB peak at 40,000 open/close pairs where Rakudo
# stays flat. `.close` now takes the handle out of the registry. A handle the
# program never closes must still reach its file at exit; the second half
# checks that in a child process.
#
# The growth is measured with `ps` (resident set size), so the first half is
# skipped where there is no `ps`. 20,000 pairs grew by about 44 MB before the
# fix and by under 1 MB after it; the limit is 16 MB.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $dir = $*TMPDIR.add("rakupp-wreg-{$*PID}");
$dir.mkdir;
LEAVE { .unlink for $dir.dir; $dir.rmdir }

sub rss-kb() {
    my $p = try run 'ps', '-o', 'rss=', '-p', ~$*PID, :out, :err;
    return Nil without $p;
    my $s = $p.out.slurp(:close).trim;
    $p.err.slurp(:close);
    $s ~~ /^\d+$/ ?? +$s !! Nil
}

my $file = $dir.add("loop.txt");
for ^2000 { my $fh = open $file, :w; $fh.print("x"); $fh.close }   # warm up
my $before = rss-kb;
if $before.defined && !$*DISTRO.is-win {
    for ^20000 { my $fh = open $file, :w; $fh.print("x"); $fh.close }
    my $grew = rss-kb() - $before;
    ck($grew < 16 * 1024, True, "20,000 closed write handles do not stay alive (grew {$grew} KB)");
}
else {
    say "ok - # SKIP no ps here";
}
ck($file.slurp, "x", 'the last write is there');

# a handle that is never closed is still flushed at exit, closed ones are not
# written twice, and the order of the rest is kept
my $prog = q:to/END/;
    my $d = @*ARGS[0].IO;
    my @keep;
    for ^100 {
        my $fh = open $d.add("u$_.txt"), :w;
        $fh.print("u$_");
        if $_ %% 2 { $fh.close } else { @keep.push($fh) }
    }
    my $fh = open $d.add("last.txt"), :w;
    $fh.print("last");
    END

my $script = $dir.add("child.raku");
$script.spurt($prog);
my $p = run $*EXECUTABLE, $script, $dir, :out, :err;
$p.out.slurp(:close); $p.err.slurp(:close);
ck($p.exitcode, 0, 'the child exits cleanly');
ck((^100).map({ $dir.add("u$_.txt").slurp }).join(","), (^100).map({ "u$_" }).join(","),
   'closed and unclosed handles all reached their files');
ck($dir.add("last.txt").slurp, "last", 'an unclosed handle is flushed at exit');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
