# Regression: the POSIX-NAMED character classes are UNICODE, not <ctype.h>.
# They were byte tests in four separate places (the rule form, the ASCII byteset
# a class compiles to, the multibyte arm of a class match, and a fourth copy on
# the grammar subrule path), so `<alpha>` stopped at the first accented letter:
# URI's RFC-3986 grammar routes every path character through <alpha>, and
# `URI.new("http://test.de/ö")` threw "Could not parse URI". Fixed by one
# predicate over a codepoint, shared by all four.
#
# The table below is Rakudo's, derived character by character. The traps:
#   <alpha> includes '_' and excludes :Nl (Ⅰ is not a letter here)
#   <punct> is :P ALONE — '+', '$', '|' are symbols and are OUT
#   <graph> is alnum ∪ punct, so symbols are out of that too
#   <print> is everything that is not :Cc — including NBSP and emoji
#   \w and <alnum> are :L ∪ :Nd ∪ '_' — '²' (:No) is NOT a word character
# Contract: exit 0 + last line PASS.
my @fail;

sub check($desc, $got, $want) { @fail.push("$desc: got $got, want $want") unless $got eqv $want }

# name => [cp, alpha, digit, space, blank, alnum, upper, lower, xdigit, punct, cntrl, graph, print]
my @table =
    ('a',    0x61, 1,0,0,0, 1,0,1,1, 0,0,1,1),
    ('Z',    0x5A, 1,0,0,0, 1,1,0,0, 0,0,1,1),
    ('5',    0x35, 0,1,0,0, 1,0,0,1, 0,0,1,1),
    ('_',    0x5F, 1,0,0,0, 1,0,0,0, 1,0,1,1),
    ('o-um', 0xF6, 1,0,0,0, 1,0,1,0, 0,0,1,1),
    ('Omega',0x3A9,1,0,0,0, 1,1,0,0, 0,0,1,1),
    ('de',   0x434,1,0,0,0, 1,0,1,0, 0,0,1,1),
    ('CJK',  0x4E2D,1,0,0,0,1,0,0,0, 0,0,1,1),
    ('dot',  0x2E, 0,0,0,0, 0,0,0,0, 1,0,1,1),
    ('arab3',0x663,0,1,0,0, 1,0,0,0, 0,0,1,1),
    ('full3',0xFF13,0,1,0,0,1,0,0,0, 0,0,1,1),
    ('sup2', 0xB2, 0,0,0,0, 0,0,0,0, 0,0,0,1),   # :No — not a digit, not alnum
    ('nbsp', 0xA0, 0,0,1,1, 0,0,0,0, 0,0,0,1),
    ('ideo-sp',0x3000,0,0,1,1,0,0,0,0,0,0,0,1),
    ('LS',   0x2028,0,0,1,0, 0,0,0,0, 0,0,0,1),  # space but not blank
    ('inv-!',0xA1, 0,0,0,0, 0,0,0,0, 1,0,1,1),
    ('euro', 0x20AC,0,0,0,0,0,0,0,0, 0,0,0,1),   # :Sc — punct and graph say no
    ('acute',0x301,0,0,0,0, 0,0,0,0, 0,0,0,1),
    ('roman-I',0x2160,0,0,0,0,0,0,0,0,0,0,0,1),  # :Nl — not alpha
    ('emoji',0x1F600,0,0,0,0,0,0,0,0,0,0,0,1),
    ('plus', 0x2B, 0,0,0,0, 0,0,0,0, 0,0,0,1),   # :Sm — NOT punct, NOT graph
    ('dollar',0x24,0,0,0,0, 0,0,0,0, 0,0,0,1),
    ('backtick',0x60,0,0,0,0,0,0,0,0, 0,0,0,1),
    ('NUL',  0x00, 0,0,0,0, 0,0,0,0, 0,1,0,0),
    ('tab',  0x09, 0,0,1,1, 0,0,0,0, 0,1,0,0),
    ('LF',   0x0A, 0,0,1,0, 0,0,0,0, 0,1,0,0),
    ('DEL',  0x7F, 0,0,0,0, 0,0,0,0, 0,1,0,0),
    ('NEL',  0x85, 0,0,1,0, 0,0,0,0, 0,1,0,0),
    ('SHY',  0xAD, 0,0,0,0, 0,0,0,0, 0,0,0,1),   # :Cf — not a control
    ('ZWSP', 0x200B,0,0,0,0,0,0,0,0, 0,0,0,1);

for @table -> @row {
    my ($nm, $cp) = @row[0, 1];
    my $c = $cp.chr;
    my @got = (so $c ~~ /^<alpha>$/),  (so $c ~~ /^<digit>$/), (so $c ~~ /^<space>$/),
              (so $c ~~ /^<blank>$/),  (so $c ~~ /^<alnum>$/), (so $c ~~ /^<upper>$/),
              (so $c ~~ /^<lower>$/),  (so $c ~~ /^<xdigit>$/),(so $c ~~ /^<punct>$/),
              (so $c ~~ /^<cntrl>$/),  (so $c ~~ /^<graph>$/), (so $c ~~ /^<print>$/);
    my @want = @row[2 .. 13].map(* == 1);
    check("$nm rules", @got, @want);
    # the `<+name>` charset form must agree with the rule form, member for member
    my @cls = (so $c ~~ /^<+alpha>$/),  (so $c ~~ /^<+digit>$/), (so $c ~~ /^<+space>$/),
              (so $c ~~ /^<+blank>$/),  (so $c ~~ /^<+alnum>$/), (so $c ~~ /^<+upper>$/),
              (so $c ~~ /^<+lower>$/),  (so $c ~~ /^<+xdigit>$/),(so $c ~~ /^<+punct>$/),
              (so $c ~~ /^<+cntrl>$/),  (so $c ~~ /^<+graph>$/), (so $c ~~ /^<+print>$/);
    check("$nm charset", @cls, @want);
}

# \w is alnum: :No and :Nl are out
check('word chars', (0xB2, 0x2160, 0x663, 0x5F, 0x4E2D, 0x301).map({ so .chr ~~ /^\w$/ }).List,
      (False, False, True, True, True, False));

# <ident> runs over Unicode letters and does not start on a digit
check('ident', ("aö1_x" ~~ /^<ident>/).Str, 'aö1_x');
check('ident-digit', so ("5a" ~~ /^<ident>/), False);

# inside a GRAMMAR the same names must resolve the same way — this path had its
# own ASCII copy, so `token inner { <alpha> }` failed where /<alpha>/ succeeded
grammar G {
    token TOP     { <unreserved>+ }
    token unreserved { <[\-._~] +alnum> }
}
check('grammar class', so G.parse('öÄ-x9'), True);
grammar H { token TOP { <inner>+ }; token inner { <alpha> } }
check('grammar rule', so H.parse('ö'), True);

# a grammar's OWN definition still wins over the built-in
grammar Shadow { token TOP { <alpha>+ }; token alpha { 'zz' } }
check('grammar shadows', so Shadow.parse('zzzz'), True);
check('grammar shadows 2', so Shadow.parse('ab'), False);

# the zero-width word assertions: <ww> only between two word chars, <wb> at an edge
check('ww', so ('ab' ~~ /a <ww> b/), True);
check('ww-edge', so ('ab' ~~ /^ <ww> ab/), False);
check('wb-start', so ('ab' ~~ /^ <wb> ab/), True);
check('wb-punct', so ('.a' ~~ /^ <wb> '.'/), False);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
