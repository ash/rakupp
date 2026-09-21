# Regression: in a `rule`, the <.ws> calls around a `%` SEPARATOR follow the
# source, and a separated quantifier always ends in one. We had both halves
# wrong in the same place (src/Regex.cpp, parseQuant's separator arm):
#
#   * the separator was wrapped in <.ws> unconditionally, so the tight
#     `rule { <n>+ % ','}` matched "1, 2" — Rakudo reads the space after the
#     separator from the SOURCE, exactly as it does between two plain atoms,
#     and that spelling has none. Roast pins it: S05-metasyntax/repeat.t's
#     "with no spaces around %, no spaces can be matched" asserts that
#     `/:s^<alpha>+%\,$/` does NOT match "a, b, c" (test 33, red until now).
#
#   * …and no <.ws> call was emitted AFTER the repetition when the source had
#     no space there, though Rakudo emits one either way: it parses the
#     separator as a quantified atom, whose sigspace treatment puts the call
#     after the whole repetition. So `rule { <n>+ % ','<[y]>}` matches
#     "1,2 y" and not "1,2y", where the plain `rule { <n>+<[y]>}` — no
#     separator, no automatic call — is the other way round.
#
# Found while answering issue #94, which reported two OTHER `%` behaviours as
# bugs (`|` pruning a branch whose prefix holds a `%` quantifier, and
# `rule { <n>+ % <op> }` refusing "1 + 2 - 3"). Both of those reproduce
# unchanged on Rakudo 2026.08 and are pinned elsewhere — this file is what
# probing around them turned up. Every expectation below was taken from the
# oracle, so the file passes under BOTH engines.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub ok($desc, $got, $want = True) { @fail.push("$desc (got {$got.raku})") unless $got eqv $want }

grammar G {
    token n  { \d+ }
    token op { '+' | '-' }

    rule spaced { <n>+ % ',' }      # a space after the separator
    rule tight  { <n>+ % ','}       # …and none
    rule spaced-cls { <n>+ % ',' <[y]> }
    rule tight-cls  { <n>+ % ','<[y]>}
    rule plain-cls  { <n>+<[y]>}    # the control: no separator at all
    rule pct2   { <n>+ %% ','}
    rule sub-sep { <n>+ % <op> }    # issue #94's second case
}
sub parses($rule, $text) { so G.parse($text, :$rule) }

# the separator's own <.ws>: only where the source puts one
ok('spaced: "1, 2"',   parses('spaced', '1, 2'));
ok('tight: "1, 2"',    parses('tight',  '1, 2'), False);
ok('tight: "1,2"',     parses('tight',  '1,2'));
ok('pct2: "1, 2"',     parses('pct2',   '1, 2'), False);
ok('pct2: "1,2,"',     parses('pct2',   '1,2,'));
# …and never one BEFORE the separator (the quantifier had no leading space)
ok('spaced: "1 , 2"',  parses('spaced', '1 , 2'), False);
ok('spaced: "1 ,2"',   parses('spaced', '1 ,2'),  False);

# the trailing <.ws> a separated quantifier always ends in
ok('tight: "1,2 "',       parses('tight',      '1,2 '));
ok('tight-cls: "1,2 y"',  parses('tight-cls',  '1,2 y'));
ok('tight-cls: "1,2y"',   parses('tight-cls',  '1,2y'),  False);
ok('spaced-cls: "1, 2 y"',parses('spaced-cls', '1, 2 y'));
# the control: a plain quantifier gets NO such call, so it is the other way round
ok('plain-cls: "12y"',    parses('plain-cls',  '12y'));
ok('plain-cls: "12 y"',   parses('plain-cls',  '12 y'), False);

# a leading space on the quantifier is the OTHER lever, and it still works:
# it distributes <.ws> into the iteration, which is what lets a space sit
# before the separator (roast S05-metasyntax/repeat.t, tests 34-35)
ok('+% spaced quantifier matches "a , b ,c"', so 'a , b ,c' ~~ /:s^ <alpha> +% \, $/);
ok('tight +% matches no spaces',              so 'a, b, c' ~~ /:s^<alpha>+%\,$/, False);

# issue #94's second case, unchanged: a subrule separator in a rule takes no
# surrounding whitespace either, so only the tight input parses
ok('sub-sep: "1 + 2 - 3"', parses('sub-sep', '1 + 2 - 3'), False);
ok('sub-sep: "1+2-3"',     parses('sub-sep', '1+2-3'));

# a token (no sigspace) is untouched by all of the above
grammar T { token n { \d+ }; token list { <n>+ % ',' } }
ok('token: "1,2"',  so T.parse('1,2',  :rule<list>));
ok('token: "1, 2"', so T.parse('1, 2', :rule<list>), False);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
