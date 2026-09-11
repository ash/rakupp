#!/usr/bin/env raku
# The round-trip property over real programs (RAKUAST-PLAN P1).
#
#     rakupp tools/rakuast-roundtrip.raku [DIR ...] [--tsv=FILE] [--verbose]
#
# For each file: `slurp.AST` builds the view, `.DEPARSE` renders it back, and
# the result is PARSED AGAIN. A program that survives all three is one the view
# describes well enough to reconstruct; anything else is named with its reason.
#
# Three outcomes, and the difference between them is the point:
#
#   view      — `.AST` threw. A construct the builder has no faithful mapping
#               for, which it says rather than guessing. This is the demand
#               list for widening, and the commonest reason early on.
#   deparse   — the view built but the renderer has no case for a class in it.
#   reparse   — BOTH ran and the text came back unparseable. That is the only
#               outcome that means something is WRONG rather than missing, so
#               it is the one this tool fails on.
#
# Runs under rakupp (it measures this engine). The counts go in the plan and
# improve; the `reparse` count is the gate and is meant to stay zero.

use experimental :rakuast;

my @dirs = @*ARGS.grep({ !.starts-with('--') });
my $tsv  = do with @*ARGS.first(*.starts-with('--tsv=')) { .substr(6) } else { Str };
my $verbose = so @*ARGS.grep('--verbose');
my $root = $?FILE.IO.parent.parent;
@dirs = <examples showcase>.map({ $root.add($_).Str }) unless @dirs;

my @files;
for @dirs -> $d {
    next unless $d.IO.d;
    for $d.IO.dir.sort -> $e {
        if $e.d { @files.append($e.dir.grep({ .Str.ends-with('.raku') }).sort) }
        elsif $e.Str.ends-with('.raku') { @files.push($e) }
    }
}

my @rows;
my %tally;
for @files -> $f {
    my $src = $f.slurp;
    my ($status, $why) = 'ok', '';
    my $ast;
    {
        $ast = $src.AST;
        CATCH { default { $status = 'view'; $why = .message.lines[0] } }
    }
    my $text;
    if $status eq 'ok' {
        { $text = $ast.DEPARSE; CATCH { default { $status = 'deparse'; $why = .message.lines[0] } } }
    }
    if $status eq 'ok' {
        # The decisive leg: the rendered text has to be Raku again. `.AST` on it
        # both re-parses and proves the view can be rebuilt from what we wrote.
        { $text.AST; CATCH { default { $status = 'reparse'; $why = .message.lines[0] } } }
    }
    %tally{$status}++;
    @rows.push: [$f.basename, $status, $why.substr(0, 120)];
    note "  {$f.basename}: $status — $why" if $verbose && $status ne 'ok';
}

with $tsv {
    $_.IO.spurt(join "\n", "file\tstatus\treason", @rows.map(*.join("\t")));
} else {
    say join "\n", "file\tstatus\treason", @rows.map(*.join("\t"));
}

note "";
note "round-trip over {+@files} files: "
   ~ %tally.sort(*.key).map({ "{.key} {.value}" }).join(', ');
my $reparse = %tally<reparse> // 0;
if $reparse {
    note "";
    note "FAILED: $reparse file(s) rendered text that will not parse — that is a WRONG";
    note "view or a wrong rendering, not a missing one:";
    note "  {.[0]}: {.[2]}" for @rows.grep(*.[1] eq 'reparse');
    exit 1;
}
note "no file rendered unparseable text — every gap is a named `view` or `deparse` miss";
