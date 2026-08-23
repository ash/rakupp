# Regression: two mis-scans in the brace-delimited regex body (`regex { … }`,
# `token { … }`), both found by the full ecosystem sweep and both ending in a
# swallow — the scan ate source past the regex, so the error (if any) surfaced
# hundreds of lines from the cause, or the following statements silently
# became part of the regex.
#
# 1. Inside a plain GROUP a quoted literal quotes as usual: the `]` in
#    `[ ']'+ ]` is a literal bracket, not the group's closer (Form's numeric
#    fields). Only inside a CHAR CLASS is a quote character a member.
# 2. `<?[…]>` / `<![…]>` are zero-width assertions holding a char class, so a
#    `[` inside them is a member (Docker::File's `<?[[]>`); and in `a?[b]` the
#    `?` is a quantifier, so its `[` opens a plain group.
#
# Oracle-verified against Rakudo 2026.07 shape by shape.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

my regex bracket-runs { [ ']'+ ]* }
ok(']]]' ~~ &bracket-runs && $/.Str eq ']]]', 'quoted ] inside a group is literal');

my regex dq-bracket { ["]"+]* }
ok(']]' ~~ &dq-bracket && $/.Str eq ']]', 'double-quoted ] inside a group too');

my regex class-then-quote { [ <-[abc]> ']'+ ] }
ok('x]' ~~ &class-then-quote, 'a char class and a quoted ] share a group');

my regex look { <?[[]> . }
ok(('[x' ~~ &look) && $/.Str eq '[', '<?[…]> is a lookahead char class');

my regex neglook { <![)]> . }
ok(('x' ~~ &neglook) && !(')' ~~ &neglook), '<![…]> is a negative lookahead');

my regex quant-group { a?[bc] }
ok('bc' ~~ &quant-group && 'abc' ~~ &quant-group, 'a?[bc] is a quantifier then a group');

my regex setop { <[a..z]-[aeiou]>+ }
ok('xyz' ~~ &setop && !('aei' ~~ &setop), 'class set-op subtraction still works');

# the statement AFTER a regex with a quoted bracket must still exist —
# the old scan consumed it into the regex without a word of complaint
my $sentinel = 42;
ok($sentinel == 42, 'the statement after such a regex still runs');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
