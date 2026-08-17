# BROKE: `rakupp evolalgo.raku` printed the seed generation and exited.
# Three cooperating gaps:
#   * a generator sequence with a STRING endpoint never stopped (`"a", { $_ ~ "x" }
#     ... "axx"` grew without bound — only a numeric `toNum()` match ended it)
#   * `for` over a lazy Seq walked only the already-materialised prefix, so
#     `$seed, {…} ... $end` was just the seed
#   * `.subst` did not wire `<?{…}>`, so `<?{ False }>` always passed and
#     evolalgo's mutate replaced every character
# FIXED: seqOp matches a non-numeric endpoint by eq/smartmatch; for pulls the
# lazy tail; substSelect evaluates code assertions.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# string-endpoint generator (the join path materialises)
check(("a", { $_ ~ "x" } ... "axxx").join(","), "a,ax,axx,axxx", "string-endpoint join");
check(("a", { $_ ~ "x" } ...^ "axxx").join(","), "a,ax,axx", "string-endpoint exclusive");
check(("hit", { $_ ~ "!" } ... "hit").join(","), "hit", "seed already at the string endpoint");

# for over a generator sequence — modifier and block, numeric and string
{
    my @n;
    @n.push($_) for 1, { $_ + 1 } ... 5;
    check(@n.join(","), "1,2,3,4,5", "modifier for over a numeric generator seq");
}
{
    my @n;
    for 1, { $_ + 1 } ... 4 { @n.push($_) }
    check(@n.join(","), "1,2,3,4", "block for over a numeric generator seq");
}
{
    my @s;
    @s.push($_) for "a", { $_ ~ "x" } ... "axx";
    check(@s.join(","), "a,ax,axx", "modifier for over a string-endpoint seq");
}

# .subst honours <?{…}> (the mutate in evolalgo.raku)
check("AAAA".subst(/<?{ False }> ./, "B", :global), "AAAA", "subst <?{ False }> does not match");
check("AAAA".subst(/<?{ True }> ./, "B", :global), "BBBB", "subst <?{ True }> matches each char");
check("AAAA".subst(/<?{ False }>/, "B", :global), "AAAA", "a zero-width failed assertion inserts nothing");
# a $var inside the assertion is the block's variable, not pattern text
# (quotemeta of 0.9 used to produce `0\.9` and the assertion never passed)
{
    my $p = 0.9;
    check("AAAA".subst(/<?{ $p > 0 }> ./, "B", :global), "BBBB", 'subst <?{ $p }> reads the variable');
    check("AAAA".subst(/<?{ $p < 0 }> ./, "B", :global), "AAAA", 'and a false $p assertion still fails');
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL'; exit 1 } else { say 'PASS' }
