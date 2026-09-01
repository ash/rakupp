# A bare-sigil declaration is spelled `<sigil>!anon` by the parser
# (Parser.cpp), so its second character is `!` and it looks exactly like a
# private attribute. When the attribute arm was hoisted above the ~100
# `ve->name == "$*LITERAL"` compares in eval's VarExpr case (a dispatch-perf
# change), it started catching these too and demanded a `self` no plain block
# has: `my %` and `my &` inside ANY block died with
# "Variable %!anon used where no 'self' is available".
#
# `my $` and `my @` never showed it — they lex as Var rather than Op and take a
# path with no read — which is why one sigil pair broke and the other did not.
# Roast: S02-names/bare-sigil.t.
my $fail = 0;
sub check($ok, $what) { $fail++ unless $ok; say ($ok ?? "ok   " !! "FAIL ") ~ $what }

for '$', '@', '%', '&' -> $sig {
    my $r = try { EVAL "\{ my $sig; 1 \}" };
    check($r === 1, "my $sig inside a block" ~ ($r === 1 ?? "" !! " — {$!}"));
}

# and at the statement level, where they always worked
check((try { EVAL 'my %; 1' }) === 1, 'my % at statement level');

# the anonymous slot still WORKS, not merely parses
check((try { EVAL 'my % = (a => 1); 1' }) === 1, 'my % with an initializer');
check((try { EVAL 'my @ = 1, 2, 3; 1' }) === 1, 'my @ with an initializer');

# a real private attribute still demands self
my $threw = False;
try { EVAL 'sub f() { $!nope }; f()' ; CATCH { default { $threw = True } } }
check($threw, 'a genuine $!attr outside a class still throws');

say $fail == 0 ?? "PASS" !! "FAIL ($fail)";
