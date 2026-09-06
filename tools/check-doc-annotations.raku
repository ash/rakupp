#!/usr/bin/env raku
# Check every `# → OUTPUT` annotation inside a ```raku block against what the
# engine actually prints.
#
# Why this exists: `tools/doc-examples-diff.raku` compares the two ENGINES with
# each other. A block where both engines agree is reported MATCH even when the
# `# →` annotation beside a line claims something neither engine ever printed —
# and phase 2 of the Grand Review found five such lines in one page alone
# (RECIPES.md). "MATCH" therefore does not mean "the documented output is
# correct"; this fills that gap. See docs/dev/findings/REVIEW-GRAND-DOCS.md.
#
#   raku tools/check-doc-annotations.raku [ENGINE] [PATH-SUBSTRING]
#
# Run it from the repo root (or set RAKUPP_ROOT).
#
# Method, deliberately conservative — it reports only what it is sure about:
#   * A block is checked only if EVERY annotated line is a `say`/`put`/`print`
#     statement whose annotation is on the SAME line. Multi-line output, `# →`
#     comments that continue a previous one, and annotations on non-printing
#     lines are listed as SKIPPED, never as failures.
#   * The block runs whole; its stdout lines are matched IN ORDER against the
#     annotations. A run that fails (a fragment, a module, a server) is SKIPPED.
#   * An annotation with a trailing parenthetical remark (`# → 42   (why)`) is
#     compared on the part before two-or-more spaces.
#   * `…` in an annotation matches anything: it is the docs' own elision mark.
#
# Exit code 0 always: this is a report, not a gate.

my $ENGINE = @*ARGS[0] // 'build-arm64/rakupp';
my $FILTER = @*ARGS[1] // '';
my $ROOT   = %*ENV<RAKUPP_ROOT> // do { # the repo root: this script sits in tools/
    my $p = $*PROGRAM.absolute.IO.parent.parent;
    ($p.add('docs').d) ?? $p.Str !! $*CWD.Str;
};
my $TMP    = $*TMPDIR.add("annot-{$*PID}");
$TMP.mkdir;

# qx{} does NOT interpolate — qqx{} does. (That bug made this scan nothing.)
my @files = qqx{find $ROOT/docs $ROOT/README.md $ROOT/LONGREAD.md -name '*.md'}
              .lines.grep(*.chars).grep({ !$FILTER || .contains($FILTER) }).sort;

my ($checked, $agree, $differ, $skipped) = 0, 0, 0, 0;
my @bad;

for @files -> $path {
    my @lines = $path.IO.lines;
    my $i = 0;
    while $i < @lines.elems {
        unless @lines[$i].starts-with('```raku') { $i++; next }
        my $start = $i + 1;
        my $end = $start;
        $end++ while $end < @lines.elems && !@lines[$end].starts-with('```');
        my @body = @lines[$start ..^ $end];
        $i = $end + 1;

        # the annotated lines, in order
        my @want;
        my $usable = True;
        for @body -> $l {
            next unless $l ~~ / '#' \s* '→' \s* (.*) $/;
            my $ann = ~$0;
            # a printing statement on the same line?
            unless $l ~~ / ^ \s* [ 'say' | 'put' | 'print' ] » / || $l ~~ / [ '.say' | '.put' ] \s* ';'? \s* '#' / {
                $usable = False;
            }
            # strip a trailing parenthetical remark: two or more spaces then text
            $ann = ~$0 if $ann ~~ / ^ (.*?) \s\s+ .* $/;
            @want.push($ann.trim);
        }
        next unless @want;
        unless $usable { $skipped++; next }

        my $file = $TMP.add("b{$checked}.raku");
        $file.spurt(@body.join("\n") ~ "\n");
        my $p = run $ENGINE, $file.Str, :out, :err;
        my $out = $p.out.slurp(:close); $p.err.slurp(:close);
        unless $p.exitcode == 0 { $skipped++; next }
        my @got = $out.lines;
        $checked++;

        # match in order, allowing … as a wildcard
        my $ok = @got.elems >= @want.elems;
        if $ok {
            for ^@want.elems -> $k {
                my ($w, $g) = @want[$k], @got[$k];
                next if $w eq $g;
                if $w.contains('…') {
                    my @parts = $w.split('…').grep(*.chars);
                    next if @parts.all.defined && all(@parts.map({ $g.contains($_) }));
                }
                $ok = False; last;
            }
        }
        if $ok { $agree++ }
        else {
            $differ++;
            @bad.push({ file => $path.subst("$ROOT/", ''), line => $start,
                        want => @want, got => @got[^min(@want.elems + 2, @got.elems)] });
        }
    }
}

say "check-annotations: $ENGINE";
say "blocks with same-line `# →` annotations checked: $checked";
say "  annotations agree with the engine: $agree";
say "  DISAGREE: $differ";
say "  skipped (not all annotations are same-line prints, or the block does not run alone): $skipped";
say '';
for @bad -> $b {
    say "--- $b<file>:$b<line>";
    say "  documented: $_" for $b<want>.list;
    say "  printed:    $_" for $b<got>.list;
}
$TMP.dir.map(*.unlink); $TMP.rmdir;
