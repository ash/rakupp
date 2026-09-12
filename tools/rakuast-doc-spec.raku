#!/usr/bin/env raku
# The `Doc::` view spec (RAKUAST-PLAN P5) — ONE FILE, TWO ENGINES.
#
#     raku  tools/rakuast-doc-spec.raku     # the oracle
#     rakupp tools/rakuast-doc-spec.raku    # us
#     …and `t/regression/rakuast-doc.raku` diffs the two.
#
# `.rakudoc` answers the `Doc::Block`s a unit carries, and upstream they ARE
# statements — `say 1; =begin rakudoc … =end rakudoc; say 2` visits
# `Statement::Expression, Doc::Block, Statement::Expression`.
#
# What is pinned here is the SHAPE: which class each pod construct becomes, the
# `type` and `level` on a block, the letter on a markup, and the nesting. The
# TEXT is deliberately not compared. Rakudo keeps a block's raw source
# including its trailing newlines (`"Heading\n\n"`), and rakupp's pod DOM —
# which predates this and which `$=pod` answers from — trims them; that is a
# difference in the pod parser, not in the view over it, and asserting it here
# would pin the wrong file.
use experimental :rakuast;

my $src = q:to/DOC/;
    =begin rakudoc
    =TITLE A small document
    =head1 Heading

    Some I<italic> and B<bold> text.

    =head2 Deeper

    =item first
    =item second
    =end rakudoc
    DOC

sub shape(Mu $n, $d = 0) {
    my $nm = $n.^name.subst('RakuAST::', '');
    my $pad = '  ' x $d;
    if $nm eq 'Doc::Block' {
        say "$pad$nm type={$n.type.raku} level={$n.level.raku}";
        for $n.paragraphs -> $p { $p ~~ Str ?? say("$pad  Str") !! shape($p, $d + 1) }
    }
    elsif $nm eq 'Doc::Paragraph' {
        say "$pad$nm";
        for $n.atoms -> $p { $p ~~ Str ?? say("$pad  Str") !! shape($p, $d + 1) }
    }
    elsif $nm eq 'Doc::Markup' {
        say "$pad$nm letter={$n.letter.raku}";
        for $n.atoms -> $p { $p ~~ Str ?? say("$pad  Str") !! shape($p, $d + 1) }
    }
    else { say "$pad$nm" }
}

my $ast = $src.AST;
say "blocks: ", $ast.rakudoc.elems;
shape($_) for $ast.rakudoc;

# …and a block among CODE is a statement like any other.
my $mixed = "say 1;\n=begin rakudoc\n=head1 H\n=end rakudoc\nsay 2;\n";
say "mixed rakudoc: ", $mixed.AST.rakudoc.map(*.type).join(",");
