# Regression: building Pod, parsing tables, and rendering to text.
#
# Five faults, all found behind Pod::Utils and Pod::Utilities, which build pod
# as much as they read it:
#
#   * the Pod::* classes are SYNTHESISED here — a hash carrying a `podclass` —
#     so `Pod::Block::Para.new(...)` had no candidate at all and every program
#     that CONSTRUCTS pod died on its first constructor;
#   * such a block then answered True to Map and Associative and False to
#     Pod::Block, its own parent, because the "Pod" tag was asked instead of
#     the class. `Array.new($block)` flattened it into its own pairs, and
#     `multi f(Pod::Block $x)` could not be called with one;
#   * `=begin table` was parsed as ordinary pod, so a table had no rows;
#   * a constructed Pod::FormattingCode had no `meta`, which a parsed one
#     always has, so the two were never `is-deeply` equal;
#   * `Pod::To::Text` is a CORE module in Rakudo — `use` it and `pod2text` is
#     there. Here there was no such module and nothing to install.
#
# Contract: exit 0 + last line PASS.
use Pod::To::Text;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# ---- constructing ----------------------------------------------------------
my $para = Pod::Block::Para.new(contents => ["block"]);
check $para.WHAT.^name, 'Pod::Block::Para', 'a Para can be constructed';
check $para.contents.List, ("block",),      'carrying its contents';
check Pod::Block::Code.new(contents => ["x"]).contents.List, ("x",), 'so can a Code';
my $named = Pod::Block::Named.new(name => 'TITLE', contents => [$para]);
check $named.name, 'TITLE', 'a Named keeps its name';
check $named.contents.elems, 1, 'and holds its child as ONE element';

# A FormattingCode always has a meta, empty when there is no `|` half — which
# is what the parser produces, so the two spellings compare equal.
check Pod::FormattingCode.new(type => 'B', contents => ["b"]).meta.List, (),
      'a constructed FormattingCode has an empty meta';
check Pod::FormattingCode.new(type => 'L', contents => ["t"], meta => ["u"]).meta.List,
      ("u",), 'and keeps one that is given';

# ---- what a Pod block IS ---------------------------------------------------
check ($para ~~ Pod::Block::Para), True,  'a Para is a Para';
check ($para ~~ Pod::Block),       True,  'and a Pod::Block';
check ($para ~~ Any),              True,  'and Any';
check ($para ~~ Hash),             False, 'but not a Hash';
check Array.new($para).elems,      1,     'so Array.new does not flatten it';
check ([$para, $para].elems),      2,     'and a list of them stays a list';
sub takes-block(Pod::Block $b) { 'ok' }
check takes-block($para), 'ok', 'it binds to a Pod::Block parameter';

# ---- parsing ---------------------------------------------------------------
use MONKEY-SEE-NO-EVAL;
sub parsed($src) { EVAL($src ~ "\n\$=pod[0].contents[0]") }

my $t = parsed q:to/POD/;
    =begin pod
    =begin table
    h1   | h2
    ============
    col1 | col2
    r2   | r3
    =end table
    =end pod
    POD
check $t.WHAT.^name, 'Pod::Block::Table', 'a table block parses as a Table';
check $t.headers.List, ("h1", "h2"),      'the row above the divider is the headers';
check $t.contents.map(*.List).List, (("col1", "col2"), ("r2", "r3")),
      'and every row below it is a row of cells';
check $t.caption, '', 'with no caption unless one is configured';

my $t2 = parsed q:to/POD/;
    =begin pod
    =begin table
    a | b
    c | d
    =end table
    =end pod
    POD
check $t2.headers.List, (), 'no divider means no headers';
check $t2.contents.map(*.List).List, (("a", "b"), ("c", "d")), 'and every line is a row';

my $t3 = parsed q:to/POD/;
    =begin pod
    =begin table
    h1      h2
    =============
    col1    col2
    =end table
    =end pod
    POD
check $t3.headers.List, ("h1", "h2"), 'columns may be aligned by spaces instead of bars';
check $t3.contents.map(*.List).List, (("col1", "col2"),), 'rows too';

check parsed(q:to/POD/).caption, 'My Cap', 'a :caption config becomes the caption';
    =begin pod
    =begin table :caption<My Cap>
    a | b
    =end table
    =end pod
    POD

check parsed(q:to/POD/).contents.map(*.List).List, (("a", "b"),), 'the abbreviated =table parses too';
    =begin pod
    =table
    a | b
    =end pod
    POD

check parsed(q:to/POD/).contents.map(*.List).List, (("x", "y"), ("z", "w")), 'and =for table';
    =begin pod
    =for table
    x | y
    z | w
    =end pod
    POD

# ---- rendering -------------------------------------------------------------
my $p = -> $s { Pod::Block::Para.new(contents => [$s]) };
check pod2text("plain"), 'plain', 'a Str renders as itself';
check pod2text($p('block')), 'block', 'a paragraph renders flat';
check pod2text(Pod::Block::Para.new(contents => ['a', 'b'])), 'ab',
      'its children run together';
check pod2text(Pod::Block::Code.new(contents => ["a\nb"])), "    a\n    b",
      'code is indented four';
check pod2text(Pod::Block::Comment.new(contents => ['hidden'])), '',
      'a comment renders to nothing';
check pod2text(Pod::Heading.new(level => 1, contents => [$p('H')])), 'H',
      'a first-level heading is flush left';
check pod2text(Pod::Heading.new(level => 3, contents => [$p('H')])), '    H',
      'and each level below it indents two more';
check pod2text(Pod::Item.new(level => 1, contents => [$p('I')])), '  * I',
      'an item is bulleted';
check pod2text(Pod::Item.new(level => 2, contents => [$p('I')])), '    * I',
      'and indents by its level';
check pod2text(Pod::FormattingCode.new(type => 'B', contents => ['bold'])), 'bold',
      'a formatting code renders its text';
check pod2text(Pod::Block::Named.new(name => 'TITLE', contents => [$p('X')])), "TITLE\nX",
      'a named block is prefixed with its name';
check pod2text(Pod::Block::Named.new(name => 'pod', contents => [$p('X')])), 'X',
      'except `pod`, which names nothing';
check pod2text(Pod::Block::Named.new(name => 'TITLE', contents => [$p('A'), $p('B')])),
      "TITLE\nA\n\nB", 'and its children are separated by a blank line';

# A whole document, which is the shape every consumer actually passes.
check EVAL(q:to/POD/ ~ "\npod2text(\$=pod)"), "The Title\n\nSome paragraph with bold and code in it.\n\n  Sub\n\n  * first\n\n  * second\n\n    my \$verbatim = 1;", 'a whole document renders';
    use Pod::To::Text;
    =begin pod
    =head1 The Title

    Some paragraph with B<bold> and C<code> in it.

    =head2 Sub

    =item first
    =item second

        my $verbatim = 1;

    =end pod
    POD

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
