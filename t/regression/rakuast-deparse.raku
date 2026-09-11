# Regression: `.DEPARSE` renders a constructed tree the way Rakudo does
# (RAKUAST-PLAN P2c).
#
# One spec, two implementations, proven by the diff being empty. The case file
# is [tools/rakuast-deparse-spec.raku](../../tools/rakuast-deparse-spec.raku) —
# 57 constructions of the classes Needle::Compile, Intl::Format::Number and
# RakuAST::Utils name. It is ENGINE-NEUTRAL: it runs under Rakudo to produce
# docs/dev/findings/rakuast/deparse-2026.08.tsv, and it runs here, and the two
# outputs have to match line for line. Keeping the cases in one file is the
# point — two copies would drift, and then the gate would be comparing this
# engine against a memory of the oracle rather than the oracle.
#
# When the oracle version moves: re-run the tool under the new Rakudo, commit
# the new TSV, and the diff against it is the work.
#
# Contract: exit 0 + last line PASS.
my $root = $?FILE.IO.parent.parent.parent;
my $spec = $root.add('tools/rakuast-deparse-spec.raku');
my $want = $root.add('docs/dev/findings/rakuast/deparse-2026.08.tsv');

my @fail;
my $p = run($*EXECUTABLE, $spec.Str, :out, :err);
my $got-text = $p.out.slurp(:close);
my $err      = $p.err.slurp(:close);
@fail.push("the spec file did not run: exit {$p.exitcode}\n$err") if $p.exitcode != 0;

my @want = $want.lines;
my @got  = $got-text.lines;

# Row by row, keyed on the label, so a missing case is named rather than
# shifting every line after it into a false mismatch.
my %got;
for @got -> $l {
    my $t = $l.index("\t");
    next unless $t.defined;
    %got{$l.substr(0, $t)} = $l.substr($t + 1);
}
for @want -> $l {
    my $t = $l.index("\t");
    next unless $t.defined;
    my $label = $l.substr(0, $t);
    my $expect = $l.substr($t + 1);
    unless %got{$label}:exists {
        @fail.push("$label: no row (the case did not run here)");
        next;
    }
    my $have = %got{$label};
    @fail.push("$label:\n     got  {$have.raku}\n     want {$expect.raku}") unless $have eq $expect;
}
# …and the other direction, so a case added here without being measured against
# the oracle cannot pass unnoticed.
for %got.keys.sort -> $k {
    @fail.push("$k: rendered here but absent from the recorded oracle — re-run the spec under Rakudo")
        unless @want.first({ .starts-with($k ~ "\t") }).defined;
}

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "{+@want} deparse cases match the 2026.08 oracle";
say "PASS";
