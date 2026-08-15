# Regression: two module-blockers, one in the regex grammar and one in the sub
# grammar.
#
# 1. The `~` GOAL operator's body is the SINGLE atom after the goal, not the rest
#    of the sequence. `'[' ~ ']' <-[\]\n]>+ <.eol>+` (Config::INI's header) means
#    `[ name ] eol+` — we deferred the goal to the END of the sequence and read
#    it as `[ name eol+ ]`. The two engines came out exactly inverted, ours
#    matching "[core\n]" and rejecting "[core]\n".
#
# 2. `sub term:<name>` DEFINES A TERM — `name` written bare, no parens, no
#    arguments. The `:<name>` part was dropped, so every such declaration was a
#    routine called `term` and a module defining more than one died
#    "Redeclaration of routine 'term'". XDG::BaseDirectory declares seven.
# Contract: exit 0 + last line PASS.
my @fail;
sub ok($desc, $got, $want = True) { @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# ---- 1. the goal operator --------------------------------------------------
grammar G {
    token eol    { [ <[#;]> \N* ]? \n }
    token header { ^^ \h* '[' ~ ']' $<text>=<-[ \] \n ]>+ \h* <.eol>+ }
    token plain  { '[' ~ ']' <-[ \] \n ]>+ }
    token after  { '[' ~ ']' <-[ \] \n ]>+ \n }
}
ok('header',            so G.parse("[core]\n", :rule<header>));
ok('header names it',   ~(G.parse("[core]\n", :rule<header>)<text>), 'core');
ok('no trailing part',  so G.parse("[core]", :rule<plain>));
ok('goal closes early', so G.parse("[core]\n", :rule<after>));
# …and the inverse must now FAIL: the newline is outside the brackets
ok('inverted rejected', so G.parse("[core\n]", :rule<after>), False);

# a goal around a group, and one nested inside another
grammar G2 {
    token pair { '(' ~ ')' [ \w+ ',' \w+ ] }
    token nest { '(' ~ ')' [ '[' ~ ']' \w+ ] }
}
ok('goal over a group', so G2.parse('(ab,cd)', :rule<pair>));
ok('nested goals',      so G2.parse('([xy])',  :rule<nest>));

# the whole Config::INI grammar, which is what found it
grammar INI {
    token TOP      { ^ <.eol>* <toplevel>? <sections>* <.eol>* $ }
    token toplevel { <keyval>* }
    token sections { <header> <keyval>* }
    token header   { ^^ \h* '[' ~ ']' $<text>=<-[ \] \n ]>+ \h* <.eol>+ }
    token keyval   { ^^ \h* <key> \h* '=' \h* <value>? \h* <.eol>+ }
    regex key      { <![#\[]> <-[;=]>+ }
    regex value    { [ <![#;]> \N ]+ }
    token eol      { [ <[#;]> \N* ]? \n }
}
my $m = INI.parse("    foo = bar\n[core]\ninur=section\n");
ok('the INI grammar',   so $m);
ok('toplevel survives', so ($m && $m<toplevel> && $m<toplevel><keyval>.elems == 1));
ok('one section',       ($m && $m<sections>) ?? $m<sections>.elems !! 0, 1);

# ---- 2. sub term:<name> ----------------------------------------------------
sub term:<alpha> { 'A' }
sub term:<beta>  { 'B' }
sub term:<data-home> { 'D' }        # a hyphen in the name is ordinary
ok('first term',  alpha, 'A');
ok('second term', beta,  'B');      # two of them: the redeclaration case
ok('kebab term',  data-home, 'D');
ok('in an expression', alpha ~ beta, 'AB');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
