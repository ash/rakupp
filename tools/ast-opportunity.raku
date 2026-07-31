#!/usr/bin/env raku
# ast-opportunity.raku — how much material would an AST-level `-O` actually have
# to work with?
#
#   rakupp tools/ast-opportunity.raku FILE... [--rakupp=PATH]
#
# Reads `rakupp --ast` for each file and counts the patterns a tree optimizer
# would rewrite. This is a STATIC count: it says how much foldable material
# exists in the source, not how often it runs. A foldable expression inside a
# hot loop is worth everything; the same expression at file scope is worth
# nothing. Treat the numbers as an upper bound on opportunity, then look at
# where the hits are before believing any of it.
#
# Patterns counted:
#   const-fold      Binary whose BOTH operands are literals        -> one literal
#   half-literal    Binary with ONE literal operand                -> specializable
#   self-update     `$x = $x <op> …`                               -> in-place, no copy
#   const-cond      If/While whose condition is a literal          -> branch removed

sub MAIN(*@files, Str :$rakupp = $*EXECUTABLE.absolute) {
    my %total;
    my @per-file;

    for @files -> $file {
        next unless $file.IO.e;
        my $out = run($rakupp, '--ast', $file, :out, :err);
        my @lines = $out.out.slurp(:close).lines;
        $out.err.slurp(:close);
        next unless @lines;

        my %n = scan(@lines);
        @per-file.push($file => %n) if %n<nodes>;
        for %n.kv -> $k, $v { %total{$k} += $v }
    }

    say "pattern         count    per 1k nodes";
    say "-" x 40;
    my $nodes = %total<nodes> // 1;
    for <const-fold half-literal self-update const-cond> -> $k {
        my $c = %total{$k} // 0;
        printf "%-14s %6d    %8.1f\n", $k, $c, $c * 1000 / $nodes;
    }
    say "-" x 40;
    printf "%-14s %6d\n", 'nodes', $nodes;
    printf "%-14s %6d\n", 'files', @per-file.elems;
}

#| Walk the indented `--ast` dump, counting rewrite opportunities.
sub scan(@lines --> Hash) {
    my %n = nodes => 0, const-fold => 0, half-literal => 0,
            self-update => 0, const-cond => 0;

    # (indent, text) for every line, so a node can look at its own children
    my @rows = @lines.map: { my $t = .subst(/^ \s+ /, ''); ($_.chars - $t.chars) / 2 => $t };
    %n<nodes> = @rows.elems;

    sub literal($t) { so $t.starts-with(any 'IntLit', 'NumLit', 'StrLit', 'BoolLit') }

    for ^@rows -> $i {
        my ($ind, $text) = @rows[$i].kv[0], @rows[$i].value;

        # the direct children of row $i: the rows after it at indent+1, stopping
        # at the first row that is not deeper
        my @kids;
        for $i ^..^ @rows.elems -> $j {
            my $ji = @rows[$j].key;
            last if $ji <= $ind;
            @kids.push(@rows[$j].value) if $ji == $ind + 1;
        }

        if $text.starts-with('Binary ') {
            my $lits = @kids.grep({ literal($_) }).elems;
            %n<const-fold>++   if @kids == 2 && $lits == 2;
            %n<half-literal>++ if @kids == 2 && $lits == 1;
        }
        elsif $text.starts-with('Assign ') && @kids == 2 {
            # `$x = $x + 1` — the target reappears as the left operand
            my $target = @kids[0];
            if $target.starts-with('VarExpr ') && @kids[1].starts-with('Binary ') {
                my $var = $target.words[1];
                # the Binary's own first child, one level deeper
                my $inner = @rows[$i + 3]:exists ?? @rows[$i + 3].value !! '';
                %n<self-update>++ if $inner.starts-with('VarExpr ') && $inner.words[1] eq $var;
            }
        }
        elsif $text.starts-with('If') || $text.starts-with('While') {
            %n<const-cond>++ if @kids && literal(@kids[0]);
        }
    }
    %n
}
