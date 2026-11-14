# `.AST` of a grammar rule (rakudo#5479, #5978, #5991). Raku++ said "`.AST`
# does not yet build a view for a grammar rule": its parser keeps a rule's
# body as text, and the view built a tree only for a body that was one quoted
# literal. The view now parses the text into Rakudo's `Regex::` nodes. Each
# rendering below is what Rakudo 2026.09's DEPARSE prints for the same body.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $n = 0;
sub body(Str $b) {
    my $d = "grammar RV{++$n} \{ token t \{$b\} \}".AST.DEPARSE;
    $d.lines[1].subst(/^ \s* 'token t { '/, '').subst(/'}' $/, '')
}

check body('X'), 'X', 'a bare literal';
check body(' X '), 'X ', 'whitespace after an atom is kept, before it is not';
check body(':my $v; X'), ':my $v; X', 'a :my statement';
check body('<foo>?'), '<foo>?', 'a quantified named assertion';
check body(' \d+ "," \w* '), '\d+ "," \w* ', 'character classes and a quote';
check body(' a | b || c '), 'a | b || c ', 'alternations';
check body(' [a b]+ % "," '), '[a b]+ % "," ', 'a group with a separator';
check body(' (x) $<n>=[y] '), '(x) $<n>=[y] ', 'a capture and a named capture';
check body(' <.ws> <?before x> <!after y> <[a..z]>+ <-[0..9]> '),
      '<.ws> <?before x> <!after y> <+[a..z]>+ <-[0..9]> ', 'assertions';
check body(' ^ x $ ^^ $$ << >> . '), '^ x $ ^^ $$ << >> . ', 'anchors';
check body(' x ** 2..3 y*? z+ '), 'x ** 2..3 y*? z+ ', 'quantifiers';
check body('abc+'), 'abc+', 'a quantifier takes the last letter';
check body("'foo'"), 'foo', 'a word in quotes comes back bare';
check body('"x y"'), '"x y"', '…and a quote with a space keeps its quotes';
check body(':ignorecase x'), ':ignorecase x', 'a modifier keeps its spelling';
check body(' | a | b '), 'a | b ', 'a leading | leaves nothing';
check body('$<x>=\d+'), '$<x>=\d+', 'a named capture of a quantified atom';
check body('\-'), '"-"', 'an escaped symbol';
check body("a # c\n b"), 'a b', 'a comment is whitespace';
check body('<( x )>'), '<( x )> ', 'match markers';

# rakudo#5479: a grammar built from its own `.AST`
my $grammar = q｢grammar G5479 { token TOP {<foo>?}; token foo { 'foo' } }｣.AST.EVAL;
my $actions = q｢class A5479 { method TOP($/) { make $<foo>.Str } }｣.AST.EVAL;
check $grammar.parse('foo', :$actions).made, 'foo', 'a grammar EVALed from its .AST parses (rakudo#5479)';

# rakudo#5978 and #5991: a unit grammar, in source order, with its docs
check q:to/END/.AST.DEPARSE, qq:to/END/, 'a unit grammar with a declarator doc (rakudo#5978)';
unit grammar T5978;
#| sample leading declarator
rule x {X}
rule y {Y}
END
unit grammar T5978;
#| sample leading declarator
rule x \{ X}
rule y \{ Y}
END
check q:to/END/.AST.DEPARSE.trim, "unit grammar G5991;\nregex x \{ :my \$v; X}\n42", 'a :my in a regex, then a statement (rakudo#5991)';
unit grammar G5991;
regex x {:my $v; X}
42; # a following statement
END

# What the view does not build yet it still refuses by name, never a wrong tree.
if $*RAKU.compiler.name eq 'Raku++' {
    check (try { "grammar RVX \{ token t \{ <foo('a')> \} \}".AST; 'built' } // $!.message.contains('does not yet build')),
          True, 'an assertion with arguments is refused by name';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
