# Regression: the six general fixes that make the Base64 module work end-to-end.
#  1. `do with (EXPR) { $^a }` — the placeholder receives the topic (statement form).
#  2. FIRST/NEXT/LAST loop phasers fire with loop semantics inside a .map block,
#     in the block's own scope (LAST sees the final iteration's params).
#  3. A Blob/Buf answers listy methods (.rotor) over its BYTES.
#  4. Multi dispatch: a candidate whose REQUIRED named is supplied beats a
#     positionally-typed rival (Rakudo narrowness); a supplied named must
#     type-match its constraint; `:buf(:$bin)` alias keys count as supplied.
#  5. A `|c` capture absorbs AND carries unclaimed named args, so samewith(|c)
#     re-passes them.
#  6. `/@arr/` interpolates as a longest-first literal alternation (~~ and .comb).
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# 1. do-with placeholder
ck((do with (3 - 2 % 3) { $^a == 3 ?? 0 !! $^a }), 1, 'do with { $^a } gets the topic');

# 2. loop phasers in .map
my @log;
my $sum = [+] (1,2,3).map(-> $c { FIRST @log.push('F'); NEXT @log.push('N'); LAST @log.push("L$c"); $c });
ck($sum, 6, 'map result unaffected by phasers');
ck(@log.join(','), 'F,N,N,N,L3', 'FIRST once, NEXT per item, LAST once w/ final param');

# 3. Blob rotor over bytes
my @chunks = Blob.new(104, 105).rotor(3, :partial);
ck(@chunks.elems, 1, 'blob rotor chunk count');
ck(@chunks[0].list.join(','), '104,105', 'blob rotor yields bytes');

# 4+5. required-named narrowness + capture carries nameds
my @trace;
proto sub f(|) {*}
multi sub f(Bool:D :$pad!, |c)  { @trace.push('pad');  f(:pad2(?$pad ?? '=' !! ''), |c) }
multi sub f(Bool:D :$str!, |c)  { @trace.push('str');  f(|c).join }
multi sub f(Str:D $s, |c)       { @trace.push('pos');  f(($s.uc,), |c) }
multi sub f(@parts, Str:D :$pad2 = '=') { @trace.push('fin'); @parts.map({ $_ ~ $pad2 }) }
my $r = f("ab", :str, :!pad);
ck($r, 'AB', 'adverb-multi chain result');
ck(@trace.join(','), 'pad,str,pos,fin', 'chain order: each adverb peeled once');
# alias :buf(:$bin) answers both names
multi sub g(Bool:D :buf(:$bin)!, |c) { 'aliased' }
multi sub g(Str:D $s) { 'plain' }
ck(g("x", :bin), 'aliased', 'alias key :buf(:$bin) satisfied by :bin');
# supplied named must type-match: Bool:D candidate must NOT take :pad("")
multi sub h(Bool:D :$w!, |c) { 'bool' }
multi sub h(Str:D $s, Str :$w) { 'str' }
ck(h("x", :w('')), 'str', 'Str named value skips the Bool:D candidate');

# 6. array interpolation in regexes
my Str @alpha = flat 'A'..'Z', 'a'..'z';
ck(("aGk" ~~ /@alpha/).Str, 'a', '~~ /@arr/ matches an element');
ck("aGk=".comb(/@alpha/).join(','), 'a,G,k', '.comb(/@arr/) extracts elements');
my @two = <ab abc>;
ck(("xabcy" ~~ /@two/).Str, 'abc', 'longest element wins (LTM)');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
