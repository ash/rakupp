# Regression: `.EVAL` runs a constructed tree in the caller's scope
# (RAKUAST-PLAN P3).
#
# Same shape as t/regression/rakuast-deparse.raku, and for the same reason:
# [tools/rakuast-eval-spec.raku](../../tools/rakuast-eval-spec.raku) is ONE
# engine-neutral case file. Run under Rakudo it produced
# docs/dev/findings/rakuast/eval-2026.08.tsv; run here it has to produce the
# same rows. Two copies of the cases would drift, and the gate would then be
# comparing this engine against a memory of the oracle.
#
# What the rows prove, beyond "it runs": the first four are Part I's scope
# probes rewritten as tree EVALs, and each answer is reachable only if the
# fragment saw the enclosing scope. A deparsed tree is mostly names from
# wherever it came from, so an EVAL that compiled in a fresh scope would carry
# nothing but literals — that is the design's load-bearing assumption, and this
# is the file that holds it up.
#
# Contract: exit 0 + last line PASS.
my $root = $?FILE.IO.parent.parent.parent;
my $spec = $root.add('tools/rakuast-eval-spec.raku');
my $want = $root.add('docs/dev/findings/rakuast/eval-2026.08.tsv');

my @fail;
my $p = run($*EXECUTABLE, $spec.Str, :out, :err);
my $got-text = $p.out.slurp(:close);
my $err      = $p.err.slurp(:close);
@fail.push("the spec file did not run: exit {$p.exitcode}\n$err") if $p.exitcode != 0;

my %got;
for $got-text.lines -> $l {
    my $t = $l.index("\t");
    %got{$l.substr(0, $t)} = $l.substr($t + 1) if $t.defined;
}
my @want = $want.lines;
for @want -> $l {
    my $t = $l.index("\t");
    next unless $t.defined;
    my ($label, $expect) = $l.substr(0, $t), $l.substr($t + 1);
    unless %got{$label}:exists { @fail.push("$label: no row (the case did not run here)"); next }
    @fail.push("$label: got {%got{$label}.raku} want {$expect.raku}") unless %got{$label} eq $expect;
}
for %got.keys.sort -> $k {
    @fail.push("$k: ran here but absent from the recorded oracle — re-run the spec under Rakudo")
        unless @want.first({ .starts-with($k ~ "\t") }).defined;
}

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "{+@want} eval cases match the 2026.08 oracle";
say "PASS";
