# Two ways `.subst` / `.match(:g)` / `s///` disagreed with `~~` on one regex.
#
# 1. `<{ $p }>` and `<?{ $p eq … }>`: the occurrence scan pasted each `$name`
#    into the pattern as text before the code block ran, braces or not, so
#    `<{ $p }>` became `<{ b }>` — a call to a routine named b — and
#    `<?{ $p eq "b" }>` compared the bareword. Code blocks and '…' spans are
#    now left for the code to read.
# 2. `rx:P5/$pat/`: a `:P5` pattern interpolates a variable as Perl SOURCE
#    under `~~`, but `.subst` and `.match(:g)` quoted it as literal text, so
#    `.subst(rx:P5/$p/, …)` with $p = 'b+' replaced nothing.
#    nige123/cli.321.do worked round it with a hand-written gsub loop.
#    (Rakudo 2026.09 refuses `:P5`; this pins rakupp's own `~~` reading.)
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $p = 'b';
my $pp = 'b+';
check('abbbc'.subst(/<{ $p }>/, 'X'),               'aXbbc', '.subst /<{ $p }>/');
check('abbbc'.subst(/<{ $pp }>/, 'X', :g),          'aXc',   '.subst /<{ $pp }>/ :g');
check('abbbc'.match(/<{ $p }>/, :g).elems,          3,       '.match /<{ $p }>/ :g');
check('abbbc'.subst(/<?{ $p eq 'b' }> b/, 'X'),     'aXbbc', '<?{ … }> reads its variable');
check('abbbc'.subst(/<!{ $p eq 'b' }> b/, 'X'),     'abbbc', '<!{ … }> reads its variable');
{ my $s = 'abbbc'; $s ~~ s:g/<{ $p }>/X/; check($s, 'aXXXc', 's:g/<{ $p }>/'); }
check('abbbc'.subst(/$p/, 'X', :g),                 'aXXXc', 'a plain $p atom still interpolates');
check(Q[a{bc].subst(/<[{]> $p/, 'X'),               'aXc',   'a { in a character class is a member');
check(Q[a'bc].subst(/<[']> $p/, 'X'),               'aXc',   "a ' in a character class is a member");

if $*RAKU.compiler.name ne 'rakudo' {
    my $q = '(\w)=(\d)';
    check(EVAL(Q['abbbcbb'.subst(rx:P5/$pp/, 'X', :g)]),            'aXcX',       ':P5 $var in .subst');
    check(EVAL(Q['abbbcbb'.match(rx:P5/$pp/, :g).elems]),           2,            ':P5 $var in .match(:g)');
    check(EVAL(Q['a=1 b=2'.subst(rx:P5/$q/, -> $m { $m[0] ~ '=R' }, :g)]), 'a=R b=R', ':P5 captures in .subst');
    check(EVAL(Q[so 'abbbc' ~~ m:P5/$pp/]),                          True,         ':P5 $var under ~~ (unchanged)');
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
