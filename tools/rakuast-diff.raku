#!/usr/bin/env raku
# The tree oracle: how close is the `.AST` view to Rakudo's own tree?
#
#     rakupp tools/rakuast-diff.raku [DIR ...] [--tsv=FILE] [--show=FILE] [--tally]
#
# With no DIR it sweeps `examples/` and `showcase/`. `--tally` adds, after the
# table, which CLASS the missing and the extra nodes are — the one question a
# percentage cannot answer, and how the list of what is left gets written.
#
# Runs `rakupp --rakuast FILE` and `raku tools/rakuast-oracle-dump.raku FILE`
# over each program and diffs the two dumps. The number, per the plan, is
# `1 − changed-lines ÷ oracle-lines`, where "changed" is what `diff` calls a
# deletion from the oracle side: an oracle node with no counterpart at the same
# place in ours. `--show=FILE` prints one file's diff instead, which is how the
# view gets polished — pick a file, diff, fix the first differing line, repeat.
#
# That formula is RECALL — how much of Rakudo's tree we reproduce — and on its
# own it can be gamed, since a node ours has that Rakudo's has not costs nothing.
# So the `extra` column counts those too, and the summary prints them.
#
# The diff is `diff(1)` rather than a hand-rolled alignment. The first version
# compared line N to line N, which is not a diff at all: one extra node near the
# top shifts every line under it, so a tree that was right everywhere but one
# statement scored 1%. That number measured the alignment, not the view.
#
# WHAT THE NUMBER MEANS, and what it deliberately does not. Both dumps are
# SHAPE ONLY — class name and nesting, children in source order. The plan
# specified scalar attributes too, filtered by a "two-position test" (an
# attribute counts only if the same statement parsed from a different line and
# column carries the same value). That test is implemented on the oracle side
# and it does not do the job: almost all of Rakudo's compiler state is POSITION-
# INVARIANT — `begin-performed`, `parse-performed`, `sunk`, `okifnil`,
# `meta-object-produced` and two dozen more are the same in both parses — so the
# test keeps them, and an attribute-level number would be dominated by state a
# view is not supposed to reproduce. Shape is the honest measure until a better
# rule exists; `--rakuast=attrs` and `--attrs` on the oracle print them for
# anyone wanting to look.
#
# The oracle side also skips the RESOLUTION APPARATUS — `Type::Setting`,
# `Declaration::External::*`, `VarDeclaration::Implicit::*`, `IMPL::*`, `Origin`
# — for the same reason: those are what Rakudo needs to COMPILE the program, not
# what the program says, and a view of the syntax will never build them.

my $root = $?FILE.IO.parent.parent;
my @dirs = @*ARGS.grep({ !.starts-with('--') });
my $tsv  = do with @*ARGS.first(*.starts-with('--tsv='))  { .substr(6) } else { Str };
my $show = do with @*ARGS.first(*.starts-with('--show=')) { .substr(7) } else { Str };
my $tally = so @*ARGS.grep('--tally');
@dirs = <examples showcase>.map({ $root.add($_).Str }) unless @dirs || $show;

my $rakupp = $*EXECUTABLE.Str;
my $oracle = $root.add('tools/rakuast-oracle-dump.raku').Str;

sub dumps($file) {
    # `=tree` for the TREE ALONE: a bare `--rakuast` puts the Raku each node
    # renders back to in a second column, which is a reading aid and not what
    # the oracle compares.
    my $a = run($rakupp, '--rakuast=tree', $file, :out, :err);
    my $ours = $a.out.slurp(:close); $a.err.slurp(:close);
    # The oracle is a child process for the same reason the tree oracle's is:
    # `.AST` is Rakudo's compiler, so a BEGIN runs and a `use` loads.
    # A per-file cap: one pathological file must not stall the sweep. (Before
    # the oracle grew its visited set, anagrams.raku ran for twenty minutes.)
    my $b = run('/usr/bin/perl', '-e', 'alarm 120; exec @ARGV', 'raku', $oracle, $file, :out, :err);
    my $theirs = $b.out.slurp(:close); $b.err.slurp(:close);
    ($ours, $theirs, $a.exitcode, $b.exitcode)
}

# The oracle lines `diff` reports as deleted — i.e. present in Rakudo's tree and
# not at that place in ours. Normal diff format, because `--unchanged-line-format`
# is GNU-only and the diff on this machine is FreeBSD's.
sub changed($ours, $theirs, %miss-by?, %extra-by?) {   # (missing, extra)
    my $a = $*TMPDIR.add("rakuast-oracle-{$*PID}.txt");
    my $b = $*TMPDIR.add("rakuast-view-{$*PID}.txt");
    $a.spurt($theirs);
    $b.spurt($ours);
    my $p = run('diff', $a.Str, $b.Str, :out, :err);
    my $o = $p.out.slurp(:close);
    $p.err.slurp(:close);
    .unlink for $a, $b;
    my @m = $o.lines.grep(*.starts-with('<'));
    my @x = $o.lines.grep(*.starts-with('>'));
    # `--tally` wants to know WHICH class is costing the most, which is the only
    # question a percentage cannot answer.
    %miss-by{.substr(2).trim}++  for @m;
    %extra-by{.substr(2).trim}++ for @x;
    (+@m, +@x)
}

with $show {
    my ($ours, $theirs, $ro, $rb) = dumps($show);
    my $a = $*TMPDIR.add("rakuast-oracle-{$*PID}.txt");
    my $b = $*TMPDIR.add("rakuast-view-{$*PID}.txt");
    $a.spurt($theirs);
    $b.spurt($ours);
    say "--- rakudo (exit $rb)  +++ rakupp (exit $ro)";
    my $p = run('diff', '-u', '-L', 'rakudo', '-L', 'rakupp', $a.Str, $b.Str, :out);
    print $p.out.slurp(:close);
    .unlink for $a, $b;
    exit 0;
}

my @files;
for @dirs -> $d {
    next unless $d.IO.d;
    for $d.IO.dir.sort -> $e {
        if $e.d { @files.append($e.dir.grep({ .Str.ends-with('.raku') }).sort) }
        elsif $e.Str.ends-with('.raku') { @files.push($e) }
    }
}

my @rows;
my (%miss-by, %extra-by);
my ($tot-same, $tot-lines, $tot-extra) = 0, 0, 0;
for @files -> $f {
    my ($ours, $theirs, $ro, $rb) = dumps($f.Str);
    my $lines = +$theirs.lines;
    my $status = $rb != 0        ?? 'oracle-fail'
              !! $ro != 0        ?? 'view-fail'
              !! !$lines         ?? 'empty'
              !!                    'ok';
    my ($miss, $extra) = $status eq 'ok' ?? changed($ours, $theirs, %miss-by, %extra-by) !! (0, 0);
    my $same = $status eq 'ok' ?? $lines - $miss !! 0;
    if $status eq 'ok' { $tot-same += $same; $tot-lines += $lines; $tot-extra += $extra }
    @rows.push: [$f.basename, $status, $lines, $same, $extra,
                 $lines ?? sprintf('%.1f', 100 * $same / $lines) !! '0.0'];
}

my $out = join "\n", "file\tstatus\toracle-lines\tmatching\textra\tpercent",
                     @rows.map(*.join("\t"));
with $tsv { $_.IO.spurt($out ~ "\n") } else { say $out }

my %by;
%by{.[1]}++ for @rows;
note "";
note "tree oracle over {+@files} files: " ~ %by.sort(*.key).map({ "{.key} {.value}" }).join(', ');
if $tally {
    note "";
    note "Rakudo has, we do not (top 20):";
    note sprintf("  %6d  %s", .value, .key) for %miss-by.sort({ -.value }).head(20);
    note "we have, Rakudo does not (top 20):";
    note sprintf("  %6d  %s", .value, .key) for %extra-by.sort({ -.value }).head(20);
}
note "matching lines: $tot-same of $tot-lines"
   ~ ($tot-lines ?? sprintf("  (%.1f%%)", 100 * $tot-same / $tot-lines) !! "")
   ~ "; nodes ours has and Rakudo's has not: $tot-extra";
