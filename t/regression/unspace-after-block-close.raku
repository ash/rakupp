# Regression: an unspace after a block's `}` joins the lines.
#
# Rakudo's rule is that a block-closing `}` at the end of a line ends the
# statement, so the next line starts a new one. An unspace — a backslash before
# the line break — is exactly how a program says it does NOT want that, and this
# engine applied the rule anyway:
#
#     } \
#     ==> sort()
#
# P6Repl::Helper feeds a `gather` block into a sort that way and died with
# "unexpected operator in term position (got '==>')". The same rule is consulted
# in three places — infix continuation, method-call continuation, and statement
# modifiers — and the silent case is the one that matters most: `do {1} \` + a
# newline + `+ 2` answered 1, dropping the addition with no error at all.
#
# The test is the token's own spaceBefore flag: an unspaced token is the only
# way something on a LATER line carries no space before it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# The silent one: an infix continuation after a block.
my $sum = do {
    1
} \
+ 2;
check $sum, 3, 'an infix after an unspaced block-close still applies';

# The feed operator, which is what found it.
my @out;
gather {
    take $_ for 3, 1, 2;
} \
==> sort()
==> @out;
check @out.join(','), '1,2,3', 'a feed after an unspaced block-close';

# A method call continued the same way.
my $n = do {
    <gamma alpha beta>
} \
.sort.join('|');
check $n, 'alpha|beta|gamma', 'a method call after an unspaced block-close';

# A statement modifier, the third consulting site.
my $hit = 0;
{
    $hit = 1
} \
if True;
check $hit, 1, 'a statement modifier after an unspaced block-close';

# And the rule it must NOT break: with no unspace, the `}` still ends the
# statement and the next line is its own.
my @two = do {
    my $x = 5;
    $x
}
.say if False;
check @two.elems, 1, 'without the backslash the brace still ends the statement';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
