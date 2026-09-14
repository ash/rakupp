# Regression: `<{ … }>` in a regex runs its block at match time and matches the
# RESULT as a pattern. It was parsed as a zero-width no-op that never ran the
# block, so a pattern that depended on it passed or failed by accident — a
# file-header check `/^ 'SPOZ2 ' <{FORMAT-VERSION}> \n/` matched any version
# (issue #81). `<$var>`, the form it should agree with, was already right.
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the report ------------------------------------------------------------
my $v = '1.2';
constant V = '1.2';
ck(("SPOZ2 1.2" ~~ /'SPOZ2 ' <{ $v }>/).Str, "SPOZ2 1.2", 'a scalar');
ck(("SPOZ2 1.2" ~~ /'SPOZ2 ' <{ V }>/).Str, "SPOZ2 1.2", 'a constant');
ck(("SPOZ2 1.2" ~~ /'SPOZ2 ' <{ "1.2" }>/).Str, "SPOZ2 1.2", 'a literal');
ck(("42" ~~ /^ <{ '\d+' }> $/).so, True, 'a Str is regex SOURCE');
ck(("SPOZ2 1.2" ~~ /'SPOZ2 ' <$v>/).Str, "SPOZ2 1.2", '<$var> agrees');
ck(("SPOZ2 1.2\nrest" ~~ /^ 'SPOZ2 ' <{ V }> \n/).so, True, 'the header check that bit');
ck(("SPOZ2 1.3\nrest" ~~ /^ 'SPOZ2 ' <{ V }> \n/).so, False, '…and it can FAIL now');
ck(("abc" ~~ /a <{ 'b' }> c/).Str, "abc", 'the block result is consumed');
ck(("axc" ~~ /a <{ 'b' }> c/).so, False, '…so a wrong value fails the match');

# --- what the block sees and may answer ------------------------------------
ck(("abc" ~~ /a <{ 1 > 0 ?? 'b' !! 'x' }> c/).Str, "abc", 'a `>` inside the code is code');
my $m = "abab" ~~ /(ab) <{ $0 }>/;
ck($m.Str, "abab", 'the block sees the match so far: $0');
ck($m[0].Str, "ab", '…and the host keeps its own $0');
$m = "abc" ~~ /a <{ '(b)' }> c/;
ck($m.Str, "abc", 'a capture inside the result matches');
ck($m.list.elems, 0, '…but stays the callee\'s, never the host\'s $0');
ck(("aBc" ~~ /a <{ rx:i/b/ }> c/).so, True, 'a Regex value keeps its own adverbs');
# (a block text no other case uses: Rakudo caches the compiled result by its
# text alone, so a `:i` use after a plain use of the same text fails THERE)
ck(("aBCd" ~~ m:i/a <{ 'bc' }> d/).so, True, 'the host\'s :i reaches into the result');
ck(("xyz" ~~ /x <{ rx/y/ }> z/).Str, "xyz", 'a Regex value');
ck(("abbc" ~~ /a <{ 'b' }>+ c/).Str, "abbc", 'quantified');
ck(("abc" ~~ /a <{ 'b', 'c' }> c/).Str, "abc", 'a List is an alternation');
ck(("abc" ~~ /a <{ my $x = 'b'; $x }> c/).Str, "abc", 'a multi-statement block');

# --- every consumer, not just `~~` ------------------------------------------
ck("abc".subst(/<{ 'b' }>/, 'X'), "aXc", 'subst');
ck("abcb".comb(/<{ 'b' }>/).join(","), "b,b", 'comb');
grammar G { token TOP { a <{ 'b' }> c } }
ck(G.parse("abc").Str, "abc", 'in a grammar token');
grammar H { token TOP { (\w) <{ $0 }> } }
ck(H.parse("aa").Str, "aa", 'a token block sees its $0');
ck(H.parse("ab").defined, False, '…and fails when the value does not follow');
my regex bee { b }
ck(("abc" ~~ /a <&bee> c/).Str, "abc", '<&name> calls the lexical regex');

# --- no pattern at all is an error, not a silent zero-width match -----------
my $died = False;
{ "ab" ~~ /a <{ Nil }>/; CATCH { default { $died = True } } }
ck($died, True, 'an undefined result dies');
# (the null regex, `<{ '' }>`, dies on both engines too — but Rakudo's is a
# compile-time SORRY out of the EVAL that no CATCH here can hold, so it is
# not asserted)

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
