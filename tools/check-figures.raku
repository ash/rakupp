#!/usr/bin/env rakupp
# Cross-check the CURRENT headline Roast figure everywhere the docs state it.
#
# RELEASING.md step 3 refreshes the figures in nine files and then PROVES they
# agree — because a half-landed refresh looks exactly like a finished one. That
# proof is a grep for figures in their `198,791 / 218,608` shape, and it has a
# blind spot: a count written as a BARE table cell carries no denominator, so
# the pattern never reaches it. Two standing tables are written that way and
# both went stale through the v3.21.0 refresh:
#
#   docs/status/ROAST.md   | **Fully passing** | **638** | **44%** |
#   docs/guide/GUIDE.md    a v1.x-era table (528 fully passing, 238 no-TAP)
#
# The first is the exact row raku-spec's gen-dashboard.raku parses, which is why
# the dashboard put main at 638 while the tag read 643. That parser strips every
# non-digit from the cell, so the cells must STAY bare — giving them denominators
# to make the grep reach them would make the dashboard read 6431464. Hence a
# checker rather than a reformat.
#
# Scope, deliberately: only figures in the HEADLINE role — the standing tables
# and README's comparison row, which by construction state where the engine
# stands NOW. Historical prose ("v3.20.1 measured 638 / 1,464") is not checked
# and must not be: RELEASING.md's own note says the previous release's numbers
# legitimately appear in the text that documents them. A checker that flagged
# those would be noise, and noise is how a gate becomes a ritual.
#
# Usage:
#   rakupp tools/check-figures.raku              # all headline figures must agree
#   rakupp tools/check-figures.raku --expect=643 # …and must equal this run's count
# Exit 0 = agreement (and matches --expect if given). Exit 1 = not.

my $ROOT = $?FILE.IO.parent.parent;
my $expect;
for @*ARGS -> $a {
    if $a ~~ / ^ '--expect=' (\d+) $ / { $expect = +$0 }
    else { note "unknown argument: $a"; exit 2 }
}

my @docs = <
    README.md
    docs/status/ROAST.md docs/status/COUNTING.md docs/status/ROADMAP.md
    docs/guide/FEATURES.md docs/guide/GUIDE.md docs/guide/HIGHLIGHTS.md
    docs/guide/OVERVIEW.md
>.map({ $ROOT.add($_) }).grep(*.e);

my @sightings;   # [relative-path, line-number, value, how-it-is-written]

for @docs -> $doc {
    my $rel = $doc.relative($ROOT);
    for $doc.lines.kv -> $i, $line {
        my $n = $i + 1;
        my $t = $line.trim;
        # The standing table's bare cell:  | **Fully passing** | **643** | **44%** |
        if $t.lc.starts-with('| **fully passing**') {
            my @cells = $t.split('|');
            next unless @cells.elems > 2;
            my $digits = @cells[2].subst(/ <-[0..9]> /, '', :g);
            @sightings.push([$rel, $n, +$digits, 'standing table cell']) if $digits;
        }
        # README's comparison row, whose label carries the denominator:
        #   | Roast … files fully passing, of 1,464 | **643 (44%)** | 594 |
        elsif $t.lc.contains('files fully passing, of') {
            my @cells = $t.split('|');
            next unless @cells.elems > 2;
            if @cells[2] ~~ / (\d+) / {
                @sightings.push([$rel, $n, (~$0).Int, 'README comparison row']);
            }
        }
    }
}

if @sightings < 2 {
    note "check-figures: found {@sightings.elems} headline figure — expected the";
    note "standing tables in docs/status/ROAST.md and docs/guide/GUIDE.md at least.";
    note "Either those tables lost their figure, or their shape changed and these";
    note "rules no longer reach them. Both are worth knowing before a release.";
    exit 1;
}

my %by-value;
%by-value{.[2]}.push($_) for @sightings;
my @values = %by-value.keys.map(*.Int).sort;

say "check-figures: {@sightings.elems} headline figures in {@sightings.map(*.[0]).unique.elems} files";
for @values -> $v {
    for %by-value{$v}.sort({ .[0] }) -> $s {
        say sprintf('  %6d   %s:%d  (%s)', $v, $s[0], $s[1], $s[3]);
    }
}

my $bad = False;
if @values > 1 {
    say "";
    say "DISAGREEMENT: the docs state {@values.elems} different current file counts at once";
    say "({@values.join(', ')}). A refresh that landed everywhere leaves exactly one.";
    $bad = True;
}
if $expect.defined && !(@values == 1 && @values[0] == $expect) {
    say "";
    say "STALE: this run measured $expect; the docs say {@values.join(' and ')}.";
    $bad = True;
}
say "" unless $bad;
say $bad ?? "check-figures: FAILED" !! "check-figures: all headline figures agree at {@values[0]}";
exit $bad ?? 1 !! 0;
