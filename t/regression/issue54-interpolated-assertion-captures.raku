# Regression (issue #54): `<mymatch=$pattern>` is a CALL, so the interpolated
# pattern matches in its own capture frame. rakupp pasted the value in as
# `$<mymatch>=[ … ]` — a plain group — so the value's `(\S+)` became the HOST's
# `$0` and `$<mymatch>` reported the whole span. Sparrow6's check engine reads
# `$data.comb(/<mymatch=$pattern>/,:match)>>.<mymatch>>>.Slip>>.Str`, so every
# `regexp:` check with a capture answered with the whole matched line.
#
# Three separate faults produced that one wrong answer, and all three are here:
#   1. the assertion forms (`<$p>`, `<alias=$p>`, `<@a>`, `<alias=@a>`) pasted
#      instead of calling, so captures leaked out and nested nowhere
#   2. `<alias=rule>` outside a grammar recorded under the RULE name only
#   3. `Match.Slip` answered the Match itself instead of its positional captures,
#      and a Slip inside a hyper did not splice
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# ---- the issue's program, verbatim ---------------------------------------
{
    my $pattern = 'A (\S+) A';
    my $matched = "ABCDCBA".comb(/<mymatch=$pattern>/,:match)>>.<mymatch>;
    my @a = $matched>>.Slip>>.Str;
    ck @a.raku, '["BCDCB"]', 'the issue as filed';
}

# ---- 1. the assertion forms are calls, not pastes -------------------------
{
    my $p = 'A (\S+) A';
    my $m = "ABCDCBA".match(/<mymatch=$p>/);
    ck $m.list.elems, 0, 'aliased call leaks no $0 to the host';
    ck $m<mymatch>.list.elems, 1, 'the callee keeps its own $0';
    ck $m<mymatch>[0].Str, 'BCDCB', 'and it is the inner capture';
    ck $m<mymatch>.Str, 'ABCDCBA', '$<mymatch> spans the whole call';

    # the unaliased form captures nothing at all — nothing names the sub-match
    my $b = "ABCDCBA".match(/<$p>/);
    ck $b.list.elems, 0, 'bare <$p> leaks no $0';
    ck $b.hash.elems, 0, 'bare <$p> names nothing';
    ck $b.Str, 'ABCDCBA', 'bare <$p> still matches';

    # …and the same for the array spelling
    my @arr = ('A (\S+) A', 'zz');
    my $n = "ABCDCBA".match(/<w=@arr>/);
    ck $n.list.elems, 0, 'aliased <@arr> leaks no $0';
    ck $n<w>.Str, 'ABCDCBA', '<w=@arr> captures the whole alternation';
    ck $n<w>.list.elems, 1, '…with the element capture nested inside';
    my $o = "ABCDCBA".match(/<@arr>/);
    ck $o.list.elems, 0, 'bare <@arr> leaks no $0';
    ck $o.Str, 'ABCDCBA', 'bare <@arr> still matches';
}

# a call is still a call: the host can backtrack into it, `:i` reaches it, and a
# quantified one collates into a list
{
    my $g = 'x+';
    ck ("xxx" ~~ /<a=$g> 'x' $/)<a>.Str, 'xx', 'the host backtracks into the callee';
    my $lc = 'abc';
    ck ("ABC" ~~ m:i/<$lc>/).Str, 'ABC', ':i reaches the interpolated assertion';
    ck ("ABC" ~~ m:i/<z=$lc>/)<z>.Str, 'ABC', '…and the aliased one';
    my $a = 'a+';
    ck ("aa,aaa" ~~ / [<w=$a>] ** 2 % ',' /)<w>.elems, 2, 'a quantified call collates';
}

# ---- 2. `<alias=rule>` outside a grammar records under the ALIAS ----------
{
    my regex R { A (\S+) A }
    my $a = "ABCDCBA".match(/<mymatch=&R>/);
    ck $a<mymatch>.Str, 'ABCDCBA', '<alias=&R> resolves and captures under the alias';
    ck $a.hash.keys.sort.List, ('mymatch',), '…and under that name alone';

    my $b = "ABCDCBA".match(/<mymatch=.ident>/);
    ck $b<mymatch>.Str, 'ABCDCBA', '<alias=.rule> captures under the alias';
    ck $b.hash.keys.sort.List, ('mymatch',), '…the dot opting the rule name out';

    # undotted, the rule name answers too — exactly as it does inside a grammar
    my $c = "abc" ~~ /<w=alpha>/;
    ck $c.hash.keys.sort.List, ('alpha', 'w'), '<w=alpha> fills both names';
    ck $c<w>.Str, 'a', '…and the alias holds the match';
    ck ("abc" ~~ /<w=.alpha>/).hash.keys.sort.List, ('w',), '<w=.alpha> fills the alias only';
}

# ---- 3. Match list coercions, and Slip inside a hyper ---------------------
{
    my $m = "a1b2" ~~ /(\w)(\d)/;
    ck $m.List.elems, 2, 'Match.List is the positional captures';
    ck $m.Slip.elems, 2, 'Match.Slip is too';
    ck $m.Slip.Str, 'a 1', '…as a Slip, which stringifies joined';
    ck ("abc" ~~ /\w+/).Slip.elems, 0, 'no captures gives an empty Slip';
    ck $m.Capture.Str, 'a1', 'Match.Capture is the Match';

    # a Slip ELEMENT splices its mapped result into the surrounding list; a plain
    # nested list keeps its shape
    my @l = ($m,);
    ck (@l>>.Slip>>.Str).List, ('a', '1'), 'a hyper splices a Slip element';
    ck ([[1,2],]>>.Str).raku, '[["1", "2"],]', '…and leaves a plain nested list nested';
}

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
