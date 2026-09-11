#!/usr/bin/env raku
# rakuast-oracle-classes.raku — the class histogram of a corpus's RakuAST
# trees on the installed Rakudo: nodes and files per class, the 95%-coverage
# head, the <=3-node tail. Part I of RAKUAST-PLAN.md sizes the target this way.
#
#   raku tools/rakuast-oracle-classes.raku ORACLE.tsv... > classes.tsv
#
# Walks the `ok` rows of rakuast-oracle.raku's output in ONE process (a child
# per file would pay a Rakudo start-up per program). The price: state leaks
# between files inside that process — a redeclared operator, a `use` — and a
# file whose `.AST` then fails is counted in the "re-walk failed" figure on
# stderr, not in the histogram. The per-file TSV stays the authority on which
# programs tree; this is the vocabulary.
use experimental :rakuast;
my (%nodes, %files);
my ($n-files, $n-nodes, $n-fail) = 0, 0, 0;
for @*ARGS -> $tsv {
    for $tsv.IO.lines.skip -> $line {
        my @f = $line.split("\t");
        next unless @f[1] eq 'ok';
        my %c;
        sub walk($x) { %c{$x.^name}++; $x.visit-children(&walk) }
        my $ok = try { walk(slurp(@f[0]).AST(:compunit)); True };
        unless $ok { $n-fail++; next }
        $n-files++;
        for %c.kv -> $k, $v { %nodes{$k} += $v; %files{$k}++; $n-nodes += $v }
    }
}
say "class\tnodes\tfiles";
for %nodes.keys.sort({ -%nodes{$_}, $_ }) { say "{.subst(/^ 'RakuAST::'/, '')}\t{%nodes{$_}}\t{%files{$_}}" }
note "files $n-files (re-walk failed $n-fail), nodes $n-nodes, classes {%nodes.elems}";
my $acc = 0; my $head = 0;
for %nodes.values.sort(-*) { $acc += $_; $head++; last if $acc >= 0.95 * $n-nodes }
note "classes covering 95% of nodes: $head; classes with <=3 nodes: {%nodes.values.grep(* <= 3).elems}";
