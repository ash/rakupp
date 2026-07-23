# Regression: three fixes that together let the XML module load and parse.
#  1. `$<name>` in a regex is a BACKREFERENCE (match the captured text, no new
#     capture) — XML close tags: `token element { '<' <name> … '</' $<name> '>' }`.
#  2. `::(EXPR)` indirect/symbolic type as a PARAMETER constraint parses and binds
#     — XML::Node: `method reparent(::(q<XML::Element>) $parent)`.
#  3. A coercion-type ATTRIBUTE `has IO::Path() $.filename` parses and declares.
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# 1. named backreference — the capture stays singular
grammar Tag {
    token TOP  { '<' <name> '>' <body> '</' $<name> '>' }
    token name { \w+ }
    token body { \w* }
}
my $m = Tag.parse('<a>hi</a>');
ck(?$m, True, 'backref: matched');
ck($m<name>.Str, 'a', 'backref: name captured once, not doubled');
ck(Tag.parse('<a>hi</b>').defined, False, 'backref: mismatched tags fail');

# 2. indirect type as a parameter constraint
class Widget { }
sub reparent(::(q<Widget>) $w) { $w.^name }
ck(reparent(Widget.new), 'Widget', 'indirect-type param binds');

# 3. coercion-type attribute declares and stores
class Doc {
    has IO::Path() $.filename;
    has $.root;
    method fn() { $!filename.defined ?? ~$!filename !! 'none' }
}
my $d = Doc.new(filename => '/tmp/x', root => 'r');
ck($d.fn, '/tmp/x', 'coercion attribute declared and holds value');
ck($d.root, 'r', 'sibling attribute unaffected');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
