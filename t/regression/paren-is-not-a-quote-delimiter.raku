# Regression: an ADJACENT paren is a call, not a quote delimiter. `q(x)`, `qq(x)`,
# `Q(x)`, `m(a)` and `rx(a)` carry arguments — which is why Rakudo answers
# `say q("11000")` with "Undeclared routine q", and calls a declared one. We read
# them as quotes with ( ) for delimiters, so a program that declared `sub q`
# could never call it. Found while fixing issue #22.
#
# WHITESPACE turns the paren back into a delimiter, which roast states outright:
# `isa-ok(rx (o), Regex)` sits next to `throws-like 'rx(o)', X::Undeclared::Symbols,
# 'rx () requires whitespace if the delims are parens'` (S05-metasyntax/regex.t),
# and S05-substitution/subst.t asserts `ok ss (foo) = 'bar'`. Every other bracket
# pair delimits with or without the space.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
# not named `run`: that would shadow the builtin this very sub calls
sub out-of(Str $code) {
    my $p = run($*EXECUTABLE, '-e', $code, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // ''
}

# the point: a declared routine is callable by its name
check out-of('my &q = -> $x { "R:$x" }; say q("11000")'), 'R:11000', 'a declared &q is called';
check out-of('sub q($x) { "R:$x" }; say q("a")'),         'R:a',     'a declared sub q is called';
check out-of('sub m($x) { "R:$x" }; say m("b")'),         'R:b',     '…and sub m';
check out-of('sub s($x) { "R:$x" }; say s("c")'),         'R:c',     '…and sub s';

# every other delimiter still quotes
check out-of('say q (spaced)'),      'spaced', 'a SPACED paren still delimits';
check out-of('my $x = "foo"; $x ~~ ss (foo) = "bar"; say $x'), 'bar',
      'so does the spaced form of a substitution';
check out-of('say q/plain/'),        'plain',  'q// still quotes';
check out-of('say q[bracket]'),      'bracket','q[] still quotes';
check out-of('say q{brace}'),        'brace',  'q{} still quotes';
check out-of('say qq/{1+1}/'),       '2',      'qq// still interpolates';
check out-of('say qw<a b>.elems'),   '2',      'qw<> still makes a word list';
check out-of('say ("ab" ~~ m/a/).Str'), 'a',   'm// still matches';
check out-of('my $x = "aa"; $x ~~ s/a/b/; say $x'), 'ba', 's/// still substitutes';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
