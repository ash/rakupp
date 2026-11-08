# A Pod block's `.Str` and `.raku` are Rakudo's: `.Str` is Mu's,
# `Pod::Block::Named<…>`, and `.raku` the constructor that builds the block,
# `Pod::Block::Named.new(name => "para", config => {}, contents => […])`, each
# block inside it written the same way, in a list as alone. Both were the
# block's text. A declarator block's `.Str` is still its text (`~&f.WHY`).
# `V<…>` is its text, run into the text around it, as Rakudo parses it: here it
# was a code, which a test read through `.Str`.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

=para Some B<bold> text

=head1 Title I<here>

=item1 one

=begin table :caption<T>
a | b
1 | 2
=end table

=defn term
meaning

=begin pod :k<v>
Para L<link|http://x> and C<code>.
=end pod

=para a V<B<x>> b

#| doc
sub f { }

ck so($=pod[0].Str ~~ /^ 'Pod::Block::Named<' \d+ '>' $/), True, '.Str';
ck $=pod[0].raku,
   'Pod::Block::Named.new(name => "para", config => {}, contents => [Pod::Block::Para.new(config => {}, contents => ["Some ", Pod::FormattingCode.new(type => "B", meta => [], config => {}, contents => ["bold"]), " text"])])',
   'a named block';
ck $=pod[1].raku,
   'Pod::Heading.new(level => 1, config => {}, contents => [Pod::Block::Para.new(config => {}, contents => ["Title ", Pod::FormattingCode.new(type => "I", meta => [], config => {}, contents => ["here"])])])',
   'a heading';
ck $=pod[2].raku, 'Pod::Item.new(level => 1, config => {}, contents => [Pod::Block::Para.new(config => {}, contents => ["one"])])',
   'an item';
ck $=pod[3].raku,
   'Pod::Block::Table.new(caption => "T", headers => [], config => {:caption("T")}, contents => [["a", "b"], ["1", "2"]])',
   'a table';
ck $=pod[4].raku, 'Pod::Defn.new(term => "term", config => {}, contents => [Pod::Block::Para.new(config => {}, contents => ["meaning"])])',
   'a definition';
ck $=pod[5].raku,
   'Pod::Block::Named.new(name => "pod", config => {:k("v")}, contents => [Pod::Block::Para.new(config => {}, contents => ["Para ", Pod::FormattingCode.new(type => "L", meta => ["http://x"], config => {}, contents => ["link"]), " and ", Pod::FormattingCode.new(type => "C", meta => [], config => {}, contents => ["code"]), "."])])',
   'a configured block with links';
ck [$=pod[2]].raku, '[' ~ $=pod[2].raku ~ ']', 'in an Array';
ck ($=pod[2], 1).raku, '(' ~ $=pod[2].raku ~ ', 1)', 'in a List';
# V<…> is no code but its text, run into the text around it (S26-documentation/08-formattingcodes.t)
ck $=pod[6].contents[0].contents, ['a B<x> b'], 'V<…>';
ck ~&f.WHY, 'doc', "a declarator block's text";
ck &f.WHY.Str, 'doc', "…by .Str too";
ck &f.WHY.gist, 'doc', '…and by .gist';
sub g(Int $a, #= a doc
) { }
ck &g.signature.params[0].WHY.gist, 'a doc', "a parameter's";

say 'PASS';
