#!/usr/bin/env rakupp
# Re-run the documentation examples that Raku++ is KNOWN to get wrong, against
# the current binary, and report which of them still differ from Rakudo.
#
# The full conformance sweep (raku-spec's typerun.raku) runs every documented
# example on both engines and takes ~10 minutes. This runs only the examples
# already classified `rakupp-differs`, which is a couple of hundred rather than
# ~1,450, so a batch can be measured in about a minute. Use the full sweep before
# a release, when the published numbers have to be right; use this in between.
#
#   rakupp tools/recheck-divergences.raku --rakupp=build-arm64/rakupp
#   rakupp tools/recheck-divergences.raku --rakupp=… --out=/tmp/div --oracle=raku
#
# Writes one file per documented type into --out, worst-first, each entry holding
# the code, Rakudo's output and ours — which is the form the fix work reads. The
# INDEX.tsv is the ranked list of types, FIXED.tsv the ones that now agree.
#
# NOTE: this tool used to live in a scratch directory and was lost to a reboot
# mid-campaign. It is in the repo because it is part of the measurement loop, not
# a throwaway: see docs/ROAST.md for the other half of the gate.

sub run-capped(Str $exe, Str $file, Int $secs = 10 --> Str) {
    # alarm+exec rather than a Raku-level timeout: Raku++ can run away on a bad
    # example, and a wedged child must not wedge the harness
    my $p = run('/usr/bin/perl', '-e',
                'alarm shift; exec @ARGV', ~$secs, $exe, $file, :out, :err);
    my $o = $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    ($o ~ $e).trim
}

sub MAIN(
    Str  :$rakupp!,                              #= the binary under test
    Str  :$oracle = 'raku',                      #= the reference implementation
    Str  :$spec   = '/Users/ash/raku-spec',      #= checkout holding the sweep data
    Str  :$out    = 'still-failing',             #= directory for the per-type reports
    Int  :$timeout = 10,                         #= seconds per example
) {
    my $runs-file = "$spec/src/data/typerun.raku";
    my $docs-file = "$spec/src/data/typedoc.raku";
    for $runs-file, $docs-file -> $f {
        unless $f.IO.e {
            note "missing $f — pass --spec=<raku-spec checkout>";
            exit 2;
        }
    }
    my %runs = EVAL slurp $runs-file;
    my %docs = EVAL slurp $docs-file;

    # typedoc lists examples per type; typerun classifies each by INDEX into that
    # list. The index is ZERO-based over the UNFILTERED list — numbering from 1
    # silently pairs every failure with the preceding example.
    my %ex;
    for @(%docs<types> // []) -> %t {
        for @(%t<examples> // []).kv -> $idx, %e { %ex{ %t<name> }{ $idx } = %e }
    }

    my @todo;
    for @(%runs<runs>) -> @r {
        next unless @r[2] eq 'rakupp-differs';
        @todo.push(@r);
    }
    say "re-checking {@todo.elems} rakupp-differs examples against $rakupp";

    mkdir $out unless $out.IO.d;
    my (@fixed, @still);
    my $tmp = "$out/.snippet.raku";
    my $n = 0;
    for @todo -> @r {
        my $type = @r[0];
        my %e = %ex{$type}{ @r[1] } // %();
        my $pre = %e<preamble> // '';
        my $code = $pre.chars ?? $pre ~ "\n" ~ (%e<code> // '') !! (%e<code> // '');
        next unless $code.chars;
        spurt $tmp, $code;
        my $ku = run-capped($rakupp, $tmp, $timeout);
        my $ra = run-capped($oracle, $tmp, $timeout);
        if $ku eq $ra { @fixed.push("$type\t@r[1]") }
        else { @still.push([$type, @r[1], $code, $ra, $ku]) }
        $n++;
        note "  $n/{@todo.elems}" if $n %% 25;
    }

    my %byt;
    for @still -> @s { %byt{ @s[0] } //= []; %byt{ @s[0] }.push(@s) }
    my @index;
    for %byt.keys.sort -> $type {
        my $slug = $type.subst('::', '-', :g);
        my $text = "# $type — {@(%byt{$type}).elems} examples still differing\n\n";
        for @(%byt{$type}) -> @s {
            $text ~= "=" x 70 ~ "\n## $type example @s[1]\n";
            $text ~= "\n--- code ---\n@s[2]\n";
            $text ~= "\n--- Rakudo ---\n@s[3]\n";
            $text ~= "\n--- Raku++ ---\n@s[4]\n\n";
        }
        spurt "$out/$slug.txt", $text;
        @index.push("{@(%byt{$type}).elems}\t$type");
    }
    spurt "$out/INDEX.tsv", @index.sort({ -.split("\t")[0].Int }).join("\n") ~ "\n";
    spurt "$out/FIXED.tsv", @fixed.join("\n") ~ "\n";

    say "";
    say "already fixed since the sweep : {@fixed.elems}";
    say "still differing               : {@still.elems}";
    say "";
    say @index.sort({ -.split("\t")[0].Int }).head(20).join("\n");
}
