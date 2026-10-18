# A code block or code assertion in a regex sees the match so far as `$/`,
# even when the match itself publishes nothing. `.grep(Regex)` and a
# junction's eigenstates match on the caller's behalf, so no `$/` reaches the
# caller (matchVarSuppressed_) — and that flag also kept the block's own
# cursor from being bound: `<?{ $/.chars > 1 }>` read Any and every element
# passed, `$<d>` read Nil, and `$0` read Any when no earlier match had left
# one. The caller's `$/` is still untouched after.
#
# And `m:x(N)//` left every match of its inner :g search in `$/`, not the N
# it answers (none, when there are fewer than N).
#
# Found while fixing #180. Contract: exit 0 + last line PASS. Every case is
# oracle-checked against Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# (first, before any match in this file has left a `$/` to fall back on)
check <a1 b22 c333>.grep(/ (\d+) <?{ $0.chars > 1 }> /).List, <b22 c333>,
    '.grep: $0 in a code assertion, no earlier match';
check $/.defined, False, '…and the grep leaves no $/ behind';
check <a1 b22 c333>.grep(/ \d+ <?{ $/.chars > 1 }> /).List, <b22 c333>,
    '.grep: $/ in a code assertion is the cursor';
check $/.defined, False, '…still none';

check <a1 b22 c333>.grep(/ (\d+) { } <?{ $/.chars > 1 }> /).List, <b22 c333>,
    '.grep: $/ after a block';
check <a1 b22 c333>.grep(/ $<d>=\d+ <?{ $<d>.chars > 2 }> /).List, ('c333',),
    '.grep: $<d> in a code assertion';
check <a1 b22 c333>.grep(/ (\d+) <?{ $0.chars > 1 }> /, :k).List, (1, 2),
    '.grep(:k)';
my @seen;
<a1 b22>.grep(/ (\d+) { @seen.push(~$0) } /);
check @seen, ['1', '22'], '.grep: a block reads $0';
my @inner;
<a1 b22>.grep(/ \d+ { "q7" ~~ /\d/; @inner.push(~$/) } /);
check @inner, ['7', '7'], '.grep: a match inside the block sets the block’s $/';
check <a1 b22 c333>.grep(/ \d+ <?{ so <x9 y99>.grep(/ \d ** {$/.chars} /) }> /).List,
    <a1 b22 c333>, '.grep inside a code assertion inside .grep';
check ?("a22" ~~ any(/ \d+ <?{ $/.chars > 1 }> /, /zzz/)), True,
    'a junction eigenstate: $/ in a code assertion';

# the caller's $/ survives all of it
"zz" ~~ /zz/;
<a1 b22>.grep(/ \d+ <?{ $/.chars > 1 }> /);
check ~$/, 'zz', 'the caller’s $/ after .grep';
<a1 b22>.first(/ \d+ <?{ $/.chars > 1 }> /);
check ~$/, 'zz', '…after .first';
"a22" ~~ any(/ \d+ <?{ $/.chars > 1 }> /, /zzz/);
check ~$/, 'zz', '…after a junction';

# m:x(N)
my $r = "a1b2c3" ~~ m:x(2)/\d/;
check $r.elems, 2, 'm:x(2) answers two';
check $/.elems, 2, '…and $/ holds those two';
check $/.map(~*).List, ('1', '2'), '…the first two';
$r = "a1" ~~ m:x(2)/\d/;
check $/.elems, 0, 'm:x(2) with only one: $/ holds none';
"a1b2c3" ~~ m:2x/\d/;
check $/.elems, 2, 'm:2x';
"a1b2c3" ~~ m:x(2)/(\d)/;
check ~$/[1][0], '2', 'm:x(2) captures';

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
