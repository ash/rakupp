#!/usr/bin/env rakupp
# Which top-level construct stops a file parsing?
#
# Prefix bisection does not answer this: a prefix that ends inside a construct
# fails legitimately, and a `unit class` file parses at EVERY prefix, so the
# search just converges on the last construct's opening line. Instead, blank out
# one top-level construct at a time — the one whose removal lets the file parse
# is the culprit. O(constructs) parses, and it points at the real line rather
# than wherever the mis-scan finally gave up.
sub MAIN(Str $file, Str :$rakupp = '/Users/ash/raku++/build/rakupp') {
    my @lines = $file.IO.lines;
    my $tmp = $*TMPDIR.add("blame-{$*PID}.raku");

    sub parse-err(@src --> Str) {
        $tmp.spurt(@src.join("\n") ~ "\n");
        my $p = run $rakupp, '--ast', $tmp.Str, :out, :err;
        $p.out.slurp(:close);
        my $e = $p.err.slurp(:close);
        return '' unless $e ~~ /'==SORRY!=='/;
        return '' if $e ~~ /'stubbed but not defined'/ or $e ~~ /'Could not find'/;
        return $e.lines[0] // 'error';
    }

    my $base = parse-err(@lines);
    unless $base { say "$file parses"; $tmp.unlink; return }
    say "baseline: $base";

    # top-level constructs: a line starting at column 0 that opens a brace, up to
    # the line where the brace count returns to zero
    my @spans;
    my $i = 0;
    while $i < @lines.elems {
        my $l = @lines[$i];
        if $l ~~ /^ \S/ and $l.contains('{') {
            my $depth = 0;
            my $j = $i;
            repeat {
                $depth += @lines[$j].comb('{').elems - @lines[$j].comb('}').elems;
                $j++;
            } while $j < @lines.elems and $depth > 0;
            @spans.push($i => $j - 1);
            $i = $j;
        }
        else { $i++ }
    }
    say "{+@spans} top-level constructs";

    for @spans -> $s {
        my @try = @lines;
        @try[$_] = '' for $s.key .. $s.value;
        my $e = parse-err(@try);
        if !$e or $e ne $base {
            say "--- blanking lines {$s.key + 1}..{$s.value + 1} " ~ ($e ?? "changes the error to: $e" !! "makes it PARSE");
            say "  {$_ + 1}: @lines[$_]" for $s.key .. min($s.key + 8, $s.value);
            $tmp.unlink;
            return;
        }
    }
    say "no single top-level construct accounts for it";
    $tmp.unlink;
}
