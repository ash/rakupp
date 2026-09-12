# Regression: five parse shapes that RakuDoc::Render's dependency chain needs
# (RAKUAST-PLAN P5 — the ordinary failures that had to clear first).
#
# None of these is a RakuAST bug; they are the walls between rakupp and a dist
# that uses RakuAST, and the plan predicted exactly that ("RakuAST is not this
# dist's first wall"). Every expectation below was checked against Rakudo
# 2026.08 side by side.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}

# 1. A LETTER outside the hand-written range table. The list covered Latin,
#    Greek, Cyrillic, CJK and Hangul and skipped the whole U+0900-U+1CFF span,
#    so an identifier or a colonpair key in any Indic script was a parse error.
#    `RakuDoc::Numeration` keys a hash on `:ह<hi>` beside `:大<zh>`.
my %lang = :ह<hi>, :ব<bn>, :大<zh>;
check %lang.keys.sort.join(","), "ह,ব,大", "a colonpair key outside the range table";
my $ह = 5;
check $ह, 5, "…and the same letter as a variable name";

# 2. A `-` JOINS to a non-ASCII letter. `rakuIdentJoins` is a byte test, so
#    `markup-Δ` lexed as three tokens and the `Δ` became an undefined routine.
#    RakuDoc::Render keys its whole template table on names of that shape.
my $markup-Δ = 7;
check $markup-Δ, 7, "a hyphen joins to a non-ASCII letter";
# …and the string scanner has to agree with the lexer about where that name
# ends, which is the failure the rule's own comment in Lexer.h records.
check "v=$markup-Δ", "v=7", "…and the interpolation scanner agrees";
check do { my $a = 3; "$a-1" }, "3-1", "…while a digit still does NOT join";

# 3. `require` with a SYMBOLIC name and an import list. The named form took the
#    `<…>`; the symbolic one parsed a full expression, so the space before `<`
#    made it the less-than operator and ate the rest of the statement.
#    `try require ::('Data::Dump::Tree') <&ddt>` is RakuDoc::Templates'.
check (try { EVAL q[try require ::('No::Such::Module') <&ddt>; "parsed"] }) // "threw",
      "parsed", "a symbolic require takes an import list";

# 4. A TOPICALIZER as an expression. `if` in the same position already worked.
#    RakuDoc::Render builds a string out of `( given %prm<type> { when … } )`.
check (given "internal" { when 'internal' { 'this page: ' }; default { 'x' } }), 'this page: ',
      "given as a statement";
check ( given 2 { when 2 { "two" } } ), "two", "…and as an expression in parens";
check ( with 3 { $_ * 2 } ), 6, "…with, likewise";
my $w = 5;
check ($w with 1), 5, "…and the MODIFIER form still wins after an expression";

# 5. A signature LITERAL in parameter position — `:( … )`. Rakudo reads it as a
#    named parameter with no name whose target destructures with that
#    signature, and the sub-signature's variables are what the body refers to.
#    PrettyDump writes every one of its handlers that way.
my $h = -> :(Int $a, $b) { "body" };
check $h.arity, 0, "a signature-literal parameter is a named one";

# 6. The DOUBLE-ANGLE colonpair value, which interpolates where the single
#    angle does not. RakuDoc's Hilite plugin writes its own `=begin pod`
#    metadata as `:author<<Richard Hainsworth, aka finanalyst>>`.
my $who = "lizmat";
my %meta = :author<<Richard $who>>, :plain<a b>;
check %meta<author>.join("|"), "Richard|lizmat", "a double-angle value interpolates";
check %meta<plain>.join("|"), "a|b", "…and the single-angle one does not";

if @fail { .say for @fail; say "FAIL ({+@fail})"; exit 1 }
say "PASS";
