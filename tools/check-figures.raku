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
# It checks THREE things, because one of them was learned the hard way. After the
# v3.22.0 figure refresh this tool reported "all headline figures agree at 642"
# while README.md still called v3.21.0 the current release, still labelled its
# comparison column `v3.21.0`, and still carried the previous release's
# documentation-example count — all in the same table it had just checked. A
# checker that covers one cell of a row family gives the same false comfort as
# the grep it replaced.
#
# Usage:
#   rakupp tools/check-figures.raku                     # headline figures must agree
#   rakupp tools/check-figures.raku --expect=642 \
#          --examples=950 --version=3.22.0              # …and match this release
# Exit 0 = agreement (and matches every --flag given). Exit 1 = not.

my $ROOT = $?FILE.IO.parent.parent;
my ($expect, $examples, $version);
for @*ARGS -> $a {
    if    $a ~~ / ^ '--expect='   (\d+) $ /            { $expect   = +$0 }
    elsif $a ~~ / ^ '--examples=' (\d+) $ /            { $examples = +$0 }
    elsif $a ~~ / ^ '--version='  (\d+ ['.' \d+]+) $ / { $version  = ~$0 }
    else { note "unknown argument: $a"; exit 2 }
}

my @docs = <
    README.md
    docs/status/ROAST.md docs/status/COUNTING.md docs/status/ROADMAP.md
    docs/guide/FEATURES.md docs/guide/GUIDE.md docs/guide/HIGHLIGHTS.md
    docs/guide/OVERVIEW.md
>.map({ $ROOT.add($_) }).grep(*.e);

# The file denominator the standing figures are stated against. A prose figure
# is a HEADLINE figure only when it is written against this one; `584 / 1,462`
# in a history paragraph is a different denominator and not this tool's business.
constant $FILES = 1464;

my @sightings;      # [relative-path, line-number, value, how-it-is-written]
my @versions;       # the release version as the docs state it
my @examples-seen;  # the documentation-example count

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
        # the release VERSION, wherever the docs state it as current. Both of
        # these went stale through the v3.22.0 refresh while the figures beside
        # them were updated, which is the worst of both: a table labelled with
        # one release holding another's numbers.
        if $t.lc.starts-with('**status:**') && $t ~~ / 'v' (\d+ ['.' \d+]+) / {
            @versions.push([$rel, $n, ~$0, 'Status line']);
        }
        elsif $t ~~ / ^ '| |' \s* 'v' (\d+ ['.' \d+]+) \s* '|' / {
            @versions.push([$rel, $n, ~$0, 'comparison-table header']);
        }
        # the documentation-example count, the other bare cell in that same row
        # family — no denominator, so the figure grep cannot see it either
        elsif $t.lc.contains('documentation examples byte-identical')
           || $t.lc.contains('documentation examples reproduced exactly') {
            my @cells = $t.split('|');
            if @cells.elems > 2 && @cells[2] ~~ / (\d+) / {
                @examples-seen.push([$rel, $n, (~$0).Int, 'documentation-example cell']);
            }
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

# ---------------------------------------------------------------------------
# Two more shapes, both read per PARAGRAPH rather than per line, because both
# straddle a line break in the pages that carry them.
#
# (a) The MIRROR IMAGE of the bare-cell blind spot: the same headline written as
#     prose inside a sentence — `660 / 1,464 files fully pass (~45%)`. Five of
#     the eight listed files state the figure that way and three state it ONLY
#     that way, so a refresh could land in every table and leave all three
#     behind with this tool reporting agreement.
#
# (b) Whether each percentage agrees with its own numerator. A refresh moves
#     `198,791 / 218,608` and leaves the `~90%` beside it, and every check above
#     still passes: the figures agree with each other, the arithmetic adds up,
#     and the percentage is simply the wrong summary of the right numbers. That
#     is how `~90%` stood beside 199,980 / 219,294 (91%) and `~43%` beside
#     660 / 1,464 (45%) — a whole point out, in the sentence a reader takes away.
#
# Historical paragraphs are exempt from both, on the same principle as the rest
# of the tool: a paragraph that names a version, or opens `_Snapshot`, documents
# its own moment and is entitled to its own numbers. Fenced code is skipped:
# a transcript is a record of a run, not a claim about today.
sub paragraphs($doc) {
    my (@out, @cur);
    my ($start, $n, $fenced) = 1, 0, False;
    for $doc.lines -> $l {
        $n++;
        if $l.trim.starts-with('```') { $fenced = !$fenced; next }
        next if $fenced;
        if $l.trim eq '' {
            # `[$start, @cur]` would store the LIVE array and the next `@cur = ()`
            # would empty every paragraph already pushed — true on both engines
            @out.push($start => [@cur]) if @cur;
            @cur = (); $start = $n + 1;
        }
        else { @cur.push($l.trim) }
    }
    @out.push($start => [@cur]) if @cur;
    @out
}
my regex pair { (\d+ [',' \d+]*) \s* '/' \s* '~'? \s* (\d+ [',' \d+]*) }
sub plain($s) { $s.subst(',', '', :g).Int }
# Which of the two standing claims a pair states, from its denominator alone.
sub kind-of-pair($den) { $den == $FILES ?? 'file' !! $den > 50_000 ?? 'test' !! '' }
# …and which one a percentage is talking about, from the words around it.
sub kind-of-pct($ctx) {
    my $c = $ctx.lc;
    return 'file' if $c.contains('file');
    return 'test' if $c.contains('test') || $c.contains('declared') || $c.contains('assertion');
    ''
}

my $pct-bad = 0;
for @docs -> $doc {
    my $rel = $doc.relative($ROOT);
    for paragraphs($doc) -> $para {
        my ($at, @lines) = $para.key, |$para.value;
        my $all = @lines.join(' ');
        next if $all ~~ / 'v' \d+ '.' \d+ '.' \d+ / or $all.contains('Snapshot');

        # (a) the prose statement of the file bar. Table rows are excluded: the
        #     bare-cell rules above already own those, and a comparison table
        #     carries another engine's 1,419 / 1,464 in the same shape.
        my $prose = @lines.grep({ !.starts-with('|') }).join(' ');
        if $prose.lc.contains('fully pass') {
            for $prose.match(&pair, :g) -> $m {
                @sightings.push([$rel, $at, plain(~$m[0]), 'prose figure'])
                    if plain(~$m[1]) == $FILES;
            }
        }

        # (b) each PERCENTAGE against the pair it summarises. Iterating the
        #     percentages rather than the pairs is what makes this reliable:
        #     these pages write one of two shapes, `N / D (P%)` — where the
        #     percentage trails its pair, as in every comparison-table cell —
        #     and `~P% … (N / D)`, where it introduces the pair that follows.
        #     A sentence carrying BOTH standing claims puts each percentage
        #     nearer the other claim's pair than its own, so distance alone,
        #     and subject-matter keywords alone, both get it wrong.
        my @pairs = $all.match(&pair, :g).map({
            %( from => .from, to => .to, num => plain(~.[0]), den => plain(~.[1]) )
        });
        next unless @pairs;
        for $all.match(/ '~'? (\d+ ['.' \d+]?) '%' /, :g) -> $pm {
            my $said = (~$pm[0]).Rat;
            my $sub;
            # trailing form: the nearest pair before it, if only a short span of
            # non-numeric text separates them (` (`, ` | `, ` fully pass (~`).
            # A `)`, `;` or `.` in that span means the percentage is outside
            # whatever the pair was in, and belongs to what comes next instead —
            # which is the whole difference between the two claims in
            # `(199,980 / 219,294); ~45% of files fully pass (660 / 1,464)`.
            with @pairs.grep({ .<to> <= $pm.from }).tail -> %prev {
                my $gap = $all.substr(%prev<to>, $pm.from - %prev<to>);
                $sub = %prev if $gap.chars <= 30 && $gap !~~ / <[0..9);.]> /;
            }
            # leading form: the pair it introduces, close enough to be reading
            # about the same thing and with no other percentage in between
            # A `)` closes what the percentage was in, and a dash introduces a
            # contrast (`97.9% — against our 660 / 1,464`), where the pair that
            # follows is the thing being compared TO, not the thing summarised.
            without $sub {
                with @pairs.first({ .<from> >= $pm.to }) -> %next {
                    my $gap = $all.substr($pm.to, %next<from> - $pm.to);
                    $sub = %next if $gap.chars <= 60 && $gap !~~ / <[%)\x[2014]\x[2013]]> /;
                }
            }
            # a percentage of something this tool is not looking at (`0.69% of
            # the pass count`) belongs to no pair here, and is left alone
            next unless $sub && $sub<den> > 100;
            my $real = $sub<num> / $sub<den> * 100;
            # a stated percentage is only as precise as it is written: `~45%`
            # may not round to a different integer, `96.9%` to a different tenth
            my $tol = (~$said).contains('.') ?? 0.06 !! 0.55;
            if abs($said - $real) > $tol {
                say sprintf('  PERCENTAGE  %s:~%d  says %s%%, but %s / %s is %.1f%%',
                            $rel, $at, $said.Str, $sub<num>.Str, $sub<den>.Str, $real);
                $pct-bad++;
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

# ---------------------------------------------------------------------------
# The file-bucket arithmetic: fully + partial + no-TAP + timeout == denominator.
#
# This is an INTERNAL-consistency check, so unlike everything above it applies to
# historical snapshots too — a v3.21.0 paragraph is allowed to carry v3.21.0's
# numbers, but they still have to add up to the same 1,464 files. That makes it
# free of the noise that would come from checking old figures for currency.
#
# It exists because two stale figures survived the v3.22.0 refresh and neither
# the RELEASING.md grep nor the checks above could see them: docs/guide/FEATURES.md
# carried v3.21.0's `685 partial` beside v3.22.0's `642` fully passing (summing to
# 1,463), and docs/status/ROAST.md's v3.21.0 snapshot opened with `642` in a
# paragraph whose own next sentence said the count repeats at 643 — which is what
# that release shipped. Both add up wrong, and nothing was looking.
sub bucket-check(IO::Path $doc, Str $rel --> Int) {
    my $flat = $doc.slurp.subst(/\s+/, ' ', :g);
    my $seen = 0;
    for $flat.match(
            / $<p>=(\d+) ' partial, ' $<n>=(\d+) ' no-TAP, ' $<t>=(\d+) ' timeout' /,
            :g) -> $m
    {
        $seen++;
        # the nearest preceding "N / D" pair — the fully-passing count and the
        # denominator the paragraph is quoting
        my $before = $flat.substr(0, $m.from).substr(*- (200 min $m.from));
        my @pairs = $before.match(/ $<f>=(\d+) \s* '/' \s* $<d>=(\d+ [',' \d+]*) /, :g);
        unless @pairs {
            say "  $rel: '{~$m<p>} partial' with no fully-passing count near it — not checked";
            next;
        }
        my $pair  = @pairs[*-1];
        my $fully = (~$pair<f>).Int;
        my $denom = (~$pair<d>).subst(',', '', :g).Int;
        my $sum   = $fully + (~$m<p>).Int + (~$m<n>).Int + (~$m<t>).Int;
        next if $sum == $denom;
        say "";
        say "ARITHMETIC: $rel — $fully fully + {~$m<p>} partial + {~$m<n>} no-TAP + {~$m<t>} timeout";
        say "  = $sum, but the denominator beside them is $denom (off by {$sum - $denom}).";
        say "  One of those four is from a different run.";
        $bad = True;
    }
    $seen
}
my ($triples, $files) = 0, 0;
for @docs -> $doc {
    my $n = bucket-check($doc, $doc.relative($ROOT));
    if $n { $triples += $n; $files++ }
}
say "";
say "check-figures: file-bucket arithmetic verified on $triples snapshot{$triples == 1 ?? '' !! 's'} in $files file{$files == 1 ?? '' !! 's'}";

# the version the docs call current, and the documentation-example count — the
# two neighbours of the cell above, both of which went stale while it did not
sub agree(@seen, $want, Str $what, Str $flag) {
    # Finding NOTHING is a failure, not a pass. This used to `return False`
    # here, so `--version=3.23.0 --examples=950` exited 0 against a tree that
    # stated neither: the checker reported only on what its rules happened to
    # reach, which is the same false comfort as the grep it replaced — the
    # blind spot this file's own header describes, one layer up. A rule that
    # matches nothing means the doc changed shape or the figure is gone, and
    # both are worth knowing before a release.
    unless @seen {
        say "";
        say "NOT FOUND: no $what anywhere in the checked docs.";
        say "Either the figure is gone, or its shape changed and this rule no";
        say "longer reaches it. $flag cannot be verified against nothing.";
        return True;
    }
    my @distinct = @seen.map(*.[2]).unique.sort;
    say "";
    say sprintf('  %-8s %s', ~$_[2], "{$_[0]}:{$_[1]}  ({$_[3]})") for @seen;
    my $bad = False;
    if @distinct > 1 {
        say "DISAGREEMENT: the docs state {@distinct.elems} different $what at once ({@distinct.join(', ')}).";
        $bad = True;
    }
    if $want.defined && !(@distinct == 1 && @distinct[0] eqv $want) {
        say "STALE: $flag says {$want}; the docs say {@distinct.join(' and ')}.";
        $bad = True;
    }
    $bad
}
$bad = True if $pct-bad;
say "check-figures: every stated percentage matches its own numerator" unless $pct-bad;
say "";
$bad = True if agree(@versions,      $version,  'release version',            '--version');
$bad = True if agree(@examples-seen, $examples, 'documentation-example count', '--examples');

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
