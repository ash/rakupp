# `=for para` and `=begin para` are a Pod::Block::Named called "para" holding
# the Pod::Block::Para, as Rakudo makes them and as the abbreviated `=para`
# already was: they were a bare Pod::Block::Para, so `.gist` showed
# Pod::Block::Para where Rakudo shows Pod::Block::Named{:name("para")}. The
# config goes on the Named block. pod2text renders a `para` block as its
# paragraphs, as Pod::To::Text does, where it printed "para" above them.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

=for para :numbered
y

=begin para :foo<bar>
x

z
=end para

=para w

ck $=pod.map(*.^name).List, ('Pod::Block::Named' xx 3).List, 'all three are Named';
ck $=pod.map(*.name).List, ('para' xx 3).List, 'called para';
ck $=pod[0].contents.map(*.^name).List, ('Pod::Block::Para',), '=for para holds a Para';
ck $=pod[1].contents.map(*.^name).List, ('Pod::Block::Para' xx 2).List, '=begin para holds its Paras';
ck $=pod[0].config, { numbered => True }, 'the config is the Named block\'s';
ck $=pod[0].contents[0].config, {}, '…not the Para\'s';
ck $=pod[1].config, { foo => 'bar' }, '=begin para\'s config';
ck $=pod[0].contents[0].contents, ['y'], 'the text';
ck $=pod.gist.lines[0], '[Pod::Block::Named{:config("\$\{:numbered(Bool::True)}"), :name("para")}', 'the gist';

my $f = $*TMPDIR.add("pod-para-{$*PID}.raku");
$f.spurt: "=begin para\nx y\n\nz\n=end para\n\n=for para\nq\n\n=para w\n";
my $p = run $*EXECUTABLE, '--doc=Text', $f, :out;
my $text = $p.out.slurp(:close);
$f.unlink;
ck $text, "x y\n\nz\n\nq\n\nw\n", '--doc=Text renders the paragraphs, not the name';

say 'PASS';
