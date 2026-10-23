# BROKE: `--highlight` read a hyper operator byte by byte, and the `<<` that
# closes `$a >>+<< $b` was taken for a `<…>` word list in term position: up to
# the next `>` on the line became one string span, and the code after it, to
# the following quote mark, was read with strings and code swapped. A
# `<<a b>>` or `«a b»` word list was not a string at all (`<` + `<a b>` + `>`,
# and `«` + two names + `»`). FatRoast edits Roast files through these spans:
# in S03-metaops/hyper.t it left `$result` un-renamed and mangled code it took
# for a string (found 2026-10-23). A Whatever `*` was an operator, so in
# `(* < 5).map(* > 2)` the `<` began a word list and in `* / 2` a regex; and
# `1..10` was `1`, `.` and a number `.10`.
# FIXED: hyperOpEnd (src/Highlight.cpp) reads `>>+<<`, `<<~>>`, `»×«`,
# `>>[&infix:<+>]<<` as one operator; doubleAngleEnd reads `<<…>>` and `«…»`
# as one string span (over lines, with quoted words), tight after a term as a
# subscript (`%h<<a>>`, `%h«a»`). scanOperator reads a `*` in term position as
# the Whatever term, `**` after a term as one operator, and `..`, `...`,
# `..^`, `...^` as one operator. A name tight against `(` (`q($a)`), or one
# a declarator introduces (`sub s { … }`), starts no quote form. A `<<` tight
# after a prefix operator is its hyper marker (`~<<@a`); `:12<1a>` is a number.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub hl(Str $code) {
    my $p = run($*EXECUTABLE, '--highlight', '-e', $code, :out, :err);
    my $o = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $o
}
sub spans(Str $code, Str $want, Str $what) {
    my $got = hl($code);
    @fail.push("$what: {$want.raku} missing from {$got.trim.raku}")
        unless $got.contains($want);
}

# a hyper operator is one operator, and what follows it is code
spans 'say $a >>+<< $b, $c, "x >>"', '&gt;&gt;+&lt;&lt; <span class="nv">$b</span>, <span class="nv">$c</span>, <span class="s">&quot;x &gt;&gt;&quot;</span>', '>>+<<';
spans 'say $a <<~>> $b', '&lt;&lt;~&gt;&gt; <span class="nv">$b</span>', '<<~>>';
spans 'say $a >>+>> 1', '&gt;&gt;+&gt;&gt; <span class="mi">1</span>', '>>+>>';
spans 'say @a «*» @b', '«*» <span class="nv">@b</span>', '«*»';
spans 'say [<<+>>] (1,2),(3,4)', '[&lt;&lt;+&gt;&gt;] (<span class="mi">1</span>', '[<<+>>] reduce';
spans 'say @a>>.uc', '<span class="nv">@a</span>&gt;&gt;.<span class="nb">uc</span>', '>>.method is not one';
spans 'say 2 <= 3 && 4 >= 3', '<span class="mi">2</span> &lt;= <span class="mi">3</span>', 'a comparison is not one';

# a double-angle word list is one string
spans 'say <<a "b c" $x>>', '<span class="s">&lt;&lt;a &quot;b c&quot; $x&gt;&gt;</span>', '<<…>> in term position';
spans 'say «a b»', '<span class="s">«a b»</span>', '«…» in term position';
spans 'say %h<<a>>', '<span class="nv">%h</span><span class="s">&lt;&lt;a&gt;&gt;</span>', '%h<<a>>';
spans 'say %h«a»', '<span class="nv">%h</span><span class="s">«a»</span>', '%h«a»';
spans "say <<a\nb>>", "<span class=\"s\">&lt;&lt;a\nb&gt;&gt;</span>", 'over lines';
spans 'say <<a ">>" b>>; say 1', '<span class="s">&lt;&lt;a &quot;&gt;&gt;&quot; b&gt;&gt;</span>', 'a quoted closer';

# a Whatever `*` is a term, so what follows it is an operator; a range is one
# operator (the same misreads: `* < 5) … >` became a word list, `* / 2; …` a
# regex to the end of the line, `1..10` a `1` and a number `.10`)
spans 'say (1..10).grep(* < 5).map(* > 2)', '(* &lt; <span class="mi">5</span>).<span class="nb">map</span>(* &gt; <span class="mi">2</span>)', '* < 5';
spans 'my $f = * / 2; say "x"', '* / <span class="mi">2</span>; <span class="nb">say</span> <span class="s">&quot;x&quot;</span>', '* / 2';
spans 'say 1..10', '<span class="mi">1</span>..<span class="mi">10</span>', '1..10';
spans 'say 1...^5', '<span class="mi">1</span>...^<span class="mi">5</span>', '1...^5';
spans 'say 2 ** 3 < 9', '<span class="mi">2</span> ** <span class="mi">3</span> &lt; <span class="mi">9</span>', '** after a term is the power';
spans 'my @b = <a b>', '<span class="s">&lt;a b&gt;</span>', 'a word list still is one';

# a name tight against `(` is a call, and the name a declarator introduces is a
# name: neither starts a quote form (`q($a, $b)` hid both variables in a string)
spans '@q = q($foo,$bar)', '<span class="n">q</span>(<span class="nv">$foo</span>,<span class="nv">$bar</span>)', 'q(…) is a call';
spans '@q = q ($foo $bar)', '<span class="s">q ($foo $bar)</span>', 'q (…) quotes';
spans 'sub s { fail "x" }; say s()', '<span class="k">sub</span> <span class="n">s</span> { <span class="nb">fail</span>', 'sub s { … }';
spans 'method m($_) { 1 }', '<span class="n">m</span>(<span class="nv">$_</span>)', 'method m(…)';
spans '$_ ~~ s/a/b/; say m/c/', '<span class="sr">s/a/b/</span>; <span class="nb">say</span> <span class="sr">m/c/</span>', 's/// and m// still are';

# a prefix hyper is no word list; a radix number is a number
spans 'is (~<<@a), ["1"]', '(~&lt;&lt;<span class="nv">@a</span>), [<span class="s">&quot;1&quot;</span>]', '~<<@a';
spans 'say -«@b', '-«<span class="nv">@b</span>', '-«@b';
spans 'say ~<< a b >>', '~<span class="s">&lt;&lt; a b &gt;&gt;</span>', 'a spaced word list after a prefix still is one';
spans 'is :12<1a> / :12<7b>, 1', '<span class="mi">:12&lt;1a&gt;</span> / <span class="mi">:12&lt;7b&gt;</span>', ':12<1a>';

if @fail {
    note "FAILED: @fail.join('; ')";
    say 'FAIL';
}
else {
    say 'PASS';
}
