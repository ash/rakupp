# Regression: `(return (1,2), 9)` returned only `(1, 2)` — the trailing arguments
# were dropped whenever `return` sat inside a parenthesised expression.
#
# `return` in TERM position (the `… or return X` path in parsePrimary) parsed its
# operand at BP_ASSIGN, which binds tighter than the comma, so the operand stopped
# at the first comma and the rest of the list was left to the enclosing paren. The
# statement-level `return (1,2), 9` was right all along, because that path parses a
# full expression — so the two spellings of the same return disagreed. `return` is
# a LISTOP: its operand is the whole comma list. Fixed by parsing it at BP_COMMA.
#
# Found building B-Raku (a Perl-to-Raku converter), which emits fully parenthesised
# code and therefore produces this shape constantly.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- the shape that was wrong ------------------------------------------------
sub parens        { (return (1,2), 9) }
sub stmt          { return (1,2), 9 }               # already right, must stay right
check parens(), stmt(),        'parenthesised return matches the statement form';
check parens(), ((1,2), 9),    '(return (1,2), 9) keeps the trailing 9';

sub flat          { (return 1, 2, 3) }
check flat(), (1, 2, 3),       '(return 1, 2, 3) returns all three';

# --- the term-position paths that put `return` there in the first place -------
sub andop($x)     { ($x > 3 and return 'big', 'x'); 'small' }
check andop(5), ('big', 'x'),  '`and return a, b` returns the pair';
check andop(1), 'small',       '…and falls through when the guard is False';

sub orop          { (False or return 5, 6); 'nope' }
check orop(), (5, 6),          '`or return a, b` returns the pair';

sub tern($c)      { ($c ?? return 'a', 'b' !! 'c') }
check tern(1), ('a', 'b'),     'return in a ternary branch keeps its whole list';
check tern(0), 'c',            '…and the other branch is untouched';

sub modif         { ((return 1, 2) if True); 'no' }
check modif(), (1, 2),         'a statement modifier around a parenthesised return';

# --- operands looser than a comma must still be swallowed whole --------------
sub arith         { (return 1 + 2, 3 * 4) }
check arith(), (3, 12),        'each element is a full expression';
sub assign        { my $y; (return $y = 3, 4) }
check assign(), (3, 4),        'an assignment is tighter than the comma, so it binds first';
sub pairs         { (return 1 => 2, 3 => 4) }
check pairs(), (1 => 2, 3 => 4), 'pairs survive as elements';
sub rangey        { (return 1..3, 'e') }
check rangey(), (1..3, 'e'),   'a range is one element, not a flattened list';
sub slippy        { (return |(1,2), 3) }
check slippy(), (1, 2, 3),     'a slip flattens into the returned list';

# --- and the single-value forms must NOT have grown a list -------------------
sub one           { (return 7) }
check one(), 7,                '(return 7) is still the bare 7, not a 1-list';
sub rw            { (return-rw 1, 2) }
check rw(), (1, 2),            'return-rw is the same listop';

# `return` binds looser than `or`, so the `or` is NOT part of the operand:
# the sub returns 1, not a Bool. Parsing the operand at BP_COMMA must not have
# pulled it in. (rakupp's cooperative return still EVALUATES the right side of
# such an infix instead of unwinding — a separate, pre-existing bug that this
# check deliberately keeps side-effect-free so it tests only the precedence.)
sub looser        { (return 1 or Nil); 'no' }
check looser(), 1,             'return does not swallow a looser infix';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
