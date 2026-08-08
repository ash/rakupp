# BROKE: an UNDOTTED built-in char-class name (<digit>, <alpha>, <blank>, …)
# was compiled straight to a Class node by the regex compiler, before any
# grammar was in scope. A grammar's own `token blank { \h* \n }` was therefore
# ignored and the built-in horizontal-whitespace class answered in its place —
# silently, so the grammar simply failed to match and printed nothing. Rakudo
# resolves the other way: the grammar's definition wins, and the built-in is a
# fallback only for names nothing defines. (The dotted form <.blank> already
# took the subrule path and got this right, as did <ws>.)
# FIXED: these names always compile to a Subrule; nameMeta() picks the built-in
# only `if (!rule)`, and a plain regex with no grammar still reaches
# builtinRuleMatch.
# Found writing an INI grammar for the raku.online front page, where a
# `token blank` meant to match an empty line never ran.

# 1. the grammar's own token wins over the built-in of the same name
grammar G { token TOP { <blank> }; token blank { 'y' } }
die "grammar's `token blank` lost to the builtin" unless G.parse('y');
die "the builtin answered where the grammar defined its own" if G.parse(' ');

grammar A { token TOP { <alpha> }; token alpha { '9' } }
die "grammar's `token alpha` lost to the builtin" unless A.parse('9');
die "builtin <alpha> answered inside a grammar that redefines it" if A.parse('a');

# 2. a redefined <digit> that is WIDER than the builtin
grammar D { token TOP { <digit>+ }; token digit { <[0..9]> | '_' } }
die "a grammar-defined <digit> did not take underscores" unless D.parse('1_2_3');

# 3. the built-in still answers when the grammar defines nothing of that name
grammar N { token TOP { <blank>+ } }
die "builtin <blank> fallback lost inside a grammar" unless N.parse("  \t");
die "builtin <blank> matched a newline (it is horizontal ws only)" if N.parse("\n");

# 4. a plain regex, with no grammar at all, keeps the built-ins
die "plain-regex <alpha> broke"  unless 'a'  ~~ /^<alpha>$/;
die "plain-regex <xdigit> broke" unless 'f'  ~~ /^<xdigit>$/;
die "plain-regex <digit> broke"  unless '7'  ~~ /^<digit>$/;

# 5. the undotted form still CAPTURES (YAMLish decodes \xNN out of $<xdigit>)
my $m = ('a7' ~~ /<alpha><digit>/);
die "undotted built-in stopped capturing: {$m.raku}"
    unless ~$m<alpha> eq 'a' && ~$m<digit> eq '7';

# 6. …and the dotted form still does not capture, and still defers to the grammar
die "dotted <.alpha> broke" unless 'a' ~~ /^<.alpha>$/;
grammar Dot { token TOP { <.blank> }; token blank { 'z' } }
die "<.blank> ignored the grammar's own token" unless Dot.parse('z');

# 7. the shape that started it: an empty line inside a line-oriented grammar
grammar INI {
    token TOP     { <section>+ }
    token section { '[' <name> ']' \n <pair>* }
    token name    { <-[\]]>+ }
    token pair    { <key> '=' <value> \n* }
    token key     { \w+ }
    token value   { \N+ }
}
my $ini = INI.parse("[a]\nx=1\n\ny=2\n");
die "a blank line broke the INI grammar" unless $ini;
die "blank line lost a pair: {$ini<section>[0]<pair>.elems}"
    unless $ini<section>[0]<pair>.elems == 2;

say 'PASS';
