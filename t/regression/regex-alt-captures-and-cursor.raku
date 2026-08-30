# Regression: LTM-PLAN phase 5 (2026-08-30) — the four defects behind the two
# corpus programs parked on "embedded code blocks reading $0/$<name> mid-match,
# probe double-execution" (books/perl6-at-a-glance/grammar2.pl and grammar4.pl,
# both byte-identical to their Rakudo reference now). Every expectation is the
# Rakudo 2026.08 answer.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- 1. a named capture's list-ness is DECLARATIVE, not what matched ---------
grammar Twice { rule TOP { <n> <n> };      token n { \d } }
grammar Maybe { rule TOP { <n> [ <n> ]? }; token n { \d } }
grammar Alts  { rule TOP { <n> | <n> };    token n { \d } }
grammar Once  { rule TOP { <n> };          token n { \d } }
ok(Twice.parse('1 2')<n> ~~ Positional && Twice.parse('1 2')<n>.elems == 2,
   'a name reached twice is a list');
ok(Maybe.parse('1')<n> ~~ Positional && Maybe.parse('1')<n>.elems == 1,
   '…even when only one occurrence matched');
ok(!(Alts.parse('1')<n> ~~ Positional), 'alternatives are separate paths, not a list');
ok(!(Once.parse('1')<n> ~~ Positional), 'a name reached once stays a lone Match');

# --- 2. …and a MID-MATCH block sees that same shape --------------------------
my @mid;
grammar Cursor {
    rule TOP { <n> { @mid.push: $<n>.elems } '+' <n> { @mid.push: $<n>.elems } }
    token n { \d }
}
Cursor.parse('1 + 2');
ok(@mid eqv [1, 2], "a repeated name reads mid-match as the list it is (got {@mid.raku})");

# --- 3. ranking measures; it does not run the program ------------------------
# The Alt ranker probes each branch for its length. A bare `{…}` is zero-width
# and always passes, so running it there changes no measurement and fires the
# side effect twice — which is what made grammar2.pl print every line twice.
my $runs = 0;
grammar Rank {
    rule TOP { <p> }
    rule p { | 'print' <v> { $runs++ } | 'print' <i> { $runs++ } }
    token i { <:alpha>+ }
    token v { \d+ }
}
Rank.parse('print 7');
ok($runs == 1, "a winning branch's block runs once, not once per ranking probe (got $runs)");

# --- 4. positional capture numbering RESTARTS in every alternative -----------
grammar Num {
    rule TOP { <a> }
    rule a { | (\w) '=' (\d) { } | (\w) '=' (\w) { } }
}
my $m = Num.parse('y = x');
ok(~$m<a>[0] eq 'y' && ~$m<a>[1] eq 'x',
   "the second branch's groups are \$0 and \$1 too");
ok(('xy' ~~ / (a)(b) | (x)(y) /).list.map(*.Str).join(',') eq 'x,y',
   '…in a plain regex as well');
ok(('y' ~~ / (x) || (y) /).list.map(*.Str).join(',') eq 'y', '…and under ||');

# --- 5. the match's list is what MATCHED, not how many groups exist ----------
ok(('a'   ~~ / (a) | (x)(y)(z) /).list.elems == 1, 'a narrow branch has its own width');
ok(('xyz' ~~ / (a) | (x)(y)(z) /).list.elems == 3, '…and a wide one keeps all three');
ok(('a'   ~~ / (a) (b)? /).list.elems == 1, 'a trailing unset capture is not in the list');
ok(('b'   ~~ / [(a)]? (b) /).list.elems == 2, '…but a hole in the middle stays');

# --- 6. a narrower match clears the slots it does not have ------------------
# `$0..$N` are aliases of `$/`, but an earlier match defined them as real vars
# in scopes still visible from here.
"zz" ~~ / (z)(z) /;
"a" ~~ m:Perl5/a|(b)/;
ok(!$0.defined, "an unmatched capture is undefined, not the last match's");

# --- and the thing all four were costing ------------------------------------
# `| 'print' <value> { say +$<value> } | 'print' <identifier> { say %var{…} }`
# — one line of output per statement, from the branch that won.
my %var; my @said;
grammar Lang {
    rule TOP { <statement>+ %% ';' }
    rule statement { | <assignment> | <printout> }
    rule assignment { | (<id>) '=' (<val>) { %var{$0} = +$1 }
                      | (<id>) '=' (<id>)  { %var{$0} = %var{$1} } }
    rule printout   { | 'print' <val> { @said.push: +$<val> }
                      | 'print' <id>  { @said.push: %var{$<id>} } }
    token id { <:alpha>+ }
    token val { \d+ }
}
Lang.parse('x = 42; y = x; print x; print y; print 7');
ok(@said eqv [42, 42, 7], "the whole shape together (got {@said.raku})");

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
