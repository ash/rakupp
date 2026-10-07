# Regression: an enum member followed by a spaced `<=` read the rest of the
# file up to the next `>` as a word list, and CBOR::Simple stopped compiling
# ("expected {" at `$_ > 0 ?? 0x7C !! 0xFC`, its line 241).
#
# Two things met there. The parser did not know an enum member was a TERM, so
# `CBOR_Min_NInt_63Bit <= $_` was a call of a routine by that name with an
# argument still to come; and c0496b90 made a spaced `<=` after such a name
# the start of a word list (for `say <= a>`, whose first word is `=`) without
# the proof a spaced `<` already needed, a `>` closing the list before the
# statement ends. Now a member is a term, as a constant is and as Rakudo has
# it, and a spaced `<=` is a word list only when that `>` comes first.
#
# Being a term fixes what the listop reading broke all along: `LO -1`,
# `LO ~ "x"` and `north <= $v` called a routine nobody declared, and
# `if LO < $v { … }` read through its block to a `>` on a later line.
#
# The `>` proof also gives back the feed after a routine name,
# `my @o <== sort <== map { … } <== @r`, which the word-list reading had
# made a parse error: Roast's integration/advent2010-day10.t expects it to
# parse (its two checks are todo for Rakudo, which still refuses it), so it
# is not checked here, where both engines must pass.
#
# Contract: exit 0 + last line PASS. Passes under both engines (Rakudo
# 2026.09), so it doubles as an oracle.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

enum Range63 (LO => -10, HI => 10);
enum Major (Unsigned => 0, Negative => 32);
enum Dir <north south>;

# 1. CBOR::Simple's shape: a chained `<=` between members, a ternary over
#    two lines, and a `>` further down
my &write-uint = -> int $major, int $value { $major + $value };
sub encode($v) {
    my @out;
    given $v {
        if LO <= $_ <= HI {
            @out.push: $_ >= 0 ?? write-uint(Unsigned, $_)
                               !! write-uint(Negative, +^$_);
        }
        else {
            @out.push: 'wide';
        }
        @out.push: $_ > 0 ?? 'pos' !! 'neg';
    }
    @out
}
check encode(5),   [5, 'pos'],      'chained <= between members, then a later >';
check encode(-3),  [34, 'neg'],     '...and the other branch';
check encode(99),  ['wide', 'pos'], '...and outside the range';

# 2. a spaced `<=` after a member compares, with a `>` on a later line
my $v = 5;
my $le = LO <= $v;
my $gt = $v > 0;
check $le, True, 'LO <= $v is a comparison';
check $gt, True, '...and the next statement is left alone';
check (north <= $v), True, 'a word-list member compares too';
check (south > north), True, 'members compare with each other';

# 3. a spaced `<` after a member, with a block between it and a later `>`
my $seen = 0;
if LO < $v { $seen = 1 }
my $after = $v > 0;
check $seen, 1, 'if LO < $v { … } runs its block';
check $after, True, '...and does not read through it to a later >';

# 4. other infixes a listop would have taken as prefixes
check LO -1, -11, 'LO -1 subtracts';
check LO ~ "x", 'LOx', 'LO ~ "x" concatenates';
check HI +| 5, 15, 'HI +| 5 is bitwise or';

# 5. a routine still takes a word list, spaced, with `<` or a fused `<=`
sub words(*@w) { @w.join('|') }
check (words <= a b>), '=|a|b', 'a routine takes a word list starting with =';
check (words < a b >), 'a|b', '...and a spaced one';
check (words(1) <= 2), True, 'a routine call compares with <=';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
exit(@fail ?? 1 !! 0);
