# Regression: a Pod block's `.gist` is Rakudo's pod-gist, a tree of the
# block classes with their name/level/type/caption/term/config and each
# content two spaces deeper, nested in a list too. It was the text of the
# contents run together, so Pod::Load's suite, which looks for
# `Pod::FormattingCode{:type("K")}` in `@pod.gist`, failed, and Pod::To::HTML
# behind it. A table with a header row also carries `:header-row(0)` in its
# config, as Rakudo's does; a declarator block's gist stays its text.
# Every expectation below is Rakudo's (2026.09), byte for byte.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

=begin pod :foo<bar> :n(3)
=item One
=item2 Two
=head3 Head
=for comment
hidden

=begin table :caption<Tab>
a | b
=====
1 | 2
=end table

=defn Term
Definition here

  code line
  second

L<link|http://x> X<idx|a,b> Z<zero> E<lt>

=end pod

#| leading doc
sub f() { }

my $want = (
    'Pod::Block::Named{:config("\\$\\{:foo(\\"bar\\"), :n(3)}"), :name("pod")}',
    '  Pod::Item{:level("1")}',
    '    Pod::Block::Para',
    '      One',
    '  Pod::Item{:level("2")}',
    '    Pod::Block::Para',
    '      Two',
    '  Pod::Heading{:level("3")}',
    '    Pod::Block::Para',
    '      Head',
    '  Pod::Block::Comment',
    '    hidden',
    '',
    '  Pod::Block::Table{:caption("Tab"), :config("\\$\\{:caption(\\"Tab\\"), :header-row(0)}")}',
    '    ["1", "2"]',
    '  Pod::Defn{:term("Term")}',
    '    Pod::Block::Para',
    '      Definition here',
    '  Pod::Block::Code',
    '    code line',
    '    second',
    '  Pod::Block::Para',
    '    Pod::FormattingCode{:type("L")}',
    '      link',
    '     ',
    '    Pod::FormattingCode{:type("X")}',
    '      idx',
    '     ',
    '    Pod::FormattingCode{:type("Z")}',
    '      zero',
    '     ',
    '    Pod::FormattingCode{:type("E")}',
    '      <'
).join("\n") ~ "\n";

ck $=pod[0].gist, $want, 'every kind of block';
ck $=pod.gist, "[$want leading doc]", 'a list of blocks gists each one (the #| one too)';
ck $=pod[0].contents[4].config, {:caption<Tab>, :header-row(0)}, 'a table with a header row says so';
ck &f.WHY.gist, 'leading doc', 'a declarator block gists as its text';

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
