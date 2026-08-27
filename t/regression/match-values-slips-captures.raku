# `$/.values` slips a list-valued capture into its individual sub-matches.
# The POSITIONAL side already did (`(\w)*` answers the letters, not one list);
# the NAMED side did not, so `<term>+` contributed ONE List where Rakudo
# contributes each Match.
#
# That is what broke Path::Finder's glob parser: its grammar's TOP action is
# `{ make $/.values».made }`, and with the list unflattened the `».made` reached
# a List — which has no .made — so TOP made Nil and every glob matched nothing.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

grammar G {
    token TOP { <a> <b>+ (\d) }
    token a   { 'x' }
    token b   { <[yz]> }
}
my $m = G.parse('xyz1');
check $m.values.elems, 4, 'a quantified named capture contributes each sub-match';
check $m.values.map(~*).sort.List, ('1', 'x', 'y', 'z'), '…and they are the right ones';
check $m<b>.elems, 2, 'the capture itself is still the list';
check $m.list.elems, 1, '.list is unchanged';
check $m.caps.map({ .key ~ '=' ~ ~.value }).List, ('a=x', 'b=y', 'b=z', '0=1'),
      '.caps is unchanged';

# a POSITIONAL quantified capture, which already slipped
my $p = 'abc' ~~ /(\w)*/;
check $p.values.map(~*).List, ('a', 'b', 'c'), 'a positional list capture still slips';

# an UNquantified named capture is a Match, not a one-element list
my $s = G.parse('xy1');
check $s.values.elems, 3, 'one <b> contributes one value';

# the TOP-action shape this was found through
grammar Made {
    rule TOP { ^ <term>+ $ { make $/.values».made } }
    proto token term {*}
    token term:sym<*> { <sym> { make 'STAR' } }
    token term:chars  { <-[*]>+ { make ~$/ } }
}
check Made.new.parse('*.md').made.List, ('STAR', '.md'),
      "a TOP action collecting its terms' .made";

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
