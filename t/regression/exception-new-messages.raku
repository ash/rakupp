# Two built-in exceptions made with `.new` have Rakudo's message:
# X::Str::Numeric's was (Any), where Rakudo writes "Cannot convert string to
# number: <reason> in '<before><HERE><after>' (indicated by <HERE>)", and
# X::OutOfRange wrote its range by .Str, "should be in 1 2", where Rakudo
# writes it by .gist, "1..2".
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck X::Str::Numeric.new(:source<a>, :pos(0), :reason<r>).message,
   "Cannot convert string to number: r in '<HERE>a' (indicated by <HERE>)", 'X::Str::Numeric';
ck ~X::Str::Numeric.new(:source<12x>, :pos(2), :reason<bad>),
   "Cannot convert string to number: bad in '12<HERE>x' (indicated by <HERE>)", '…stringified';
ck X::OutOfRange.new(:what<x>, :got(5), :range(1..2)).message,
   'x out of range. Is: 5, should be in 1..2', 'X::OutOfRange';
ck X::OutOfRange.new(:what<x>, :got('a'), :range(1..^3)).message,
   'x out of range. Is: a, should be in 1..^3', '…an exclusive range';
ck X::OutOfRange.new(:what<x>, :got(5), :range(1..2), :comment<c>).message,
   'x out of range. Is: 5, should be in 1..2; c', '…with a comment';
try +'12x';
ck $!.message, "Cannot convert string to number: trailing characters after number in '12<HERE>x' (indicated by <HERE>)",
   'a numification (as before)';

say 'PASS';
