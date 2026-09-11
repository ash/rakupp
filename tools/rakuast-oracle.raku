#!/usr/bin/env raku
# rakuast-oracle.raku — the Rakudo half of the RakuAST tree oracle
# (docs/dev/plans/RAKUAST-PLAN.md, Part III, `--rakuast`): for every program
# in LIST, ask the installed Rakudo for its RakuAST tree and record one row.
#
#   raku tools/rakuast-oracle.raku LIST OUT.tsv        # run from the corpus root
#
# Runs under Rakudo, not rakupp — it measures the oracle. One CHILD process per
# file: `.AST` is the compiler, so BEGIN blocks execute and `use` loads modules,
# and a child isolates an exit. Synchronous `run`: a non-zero exit is a value,
# not an exception (Proc::Async's broken promise escaped `try`; and the Proc a
# pipe's `.close` returns is SUNK when unassigned, which throws for a failed
# child — hence the assignment below). The child's stdout is read from its LAST
# `OK` line, because a BEGIN { say … } lands ahead of it. The file's own
# directory and its lib/ are on the child's -I, so a program that `use`s a
# sibling module resolves it. No per-file cap. Resumes: files already in
# OUT.tsv are skipped, so a run can be repeated after a fix.
#
# Columns: file, status (ok|fail), nodes, classes, top5 (the five commonest
# classes, `RakuAST::` stripped), seconds, first-error (the reason line after
# any ===SORRY!=== header).
my ($list, $out) = @*ARGS;
my $child = q:to/RAKU/;
use experimental :rakuast;
my %c; my $n = 0;
sub walk($x) { $n++; %c{$x.^name}++; $x.visit-children(&walk) }
my $ast = slurp(@*ARGS[0]).AST(:compunit);
walk($ast);
say "OK\t$n\t{%c.elems}\t{%c.sort(-*.value).head(5).map({ .key.subst(/^ 'RakuAST::'/, '') ~ '=' ~ .value }).join(',')}";
RAKU
my %seen = $out.IO.e ?? $out.IO.lines.skip.map({ .split("\t")[0] => 1 }) !! ();
my $fh = open $out, :a;
$fh.say: "file\tstatus\tnodes\tclasses\ttop5\tseconds\tfirst-error" unless %seen;
my $done = 0;
for $list.IO.lines -> $f {
    next if %seen{$f};
    my $t0  = now;
    my $dir = $f.IO.parent;
    my $abs = $dir.absolute;                # -I must be absolute: the child's cwd IS $dir
    my $pr  = run 'raku', "-I$abs", "-I$abs/lib", '-e', $child, $f.IO.absolute,
                  :in, :out, :err, :cwd($dir);
    my $closed = $pr.in.close;             # unassigned, a failed Proc is sunk and throws
    my $o = $pr.out.slurp(:close);
    my $e = $pr.err.slurp(:close);
    my $secs = (now - $t0).round(0.01);
    my $ok-line = $o.lines.grep(*.starts-with("OK\t")).tail;
    if $pr.exitcode == 0 && $ok-line {
        $fh.say: "$f\tok\t{$ok-line.split("\t")[1..3].join("\t")}\t$secs\t";
    }
    else {
        my $err = $e.lines.grep(*.chars).grep({ $_ !~~ /^ '===SORRY!===' / }).head // '';
        $fh.say: "$f\tfail\t\t\t\t$secs\t{$err.subst(/\t/, ' ', :g).substr(0, 160)}";
    }
    $fh.flush;
    $done++;
}
$fh.close;
say "done $done files";
