# `substr`'s out-of-range messages are Rakudo's. A start past the end now
# ends "; use *-10 if you want to index relative to the end", as a negative
# one within the string's length does ("use *-3"); one further back than that
# gets no hint, the message ending "; ". A negative length is the "Number of
# characters argument", in 0..^Inf, where it was "Length argument … should be
# in 0..2"; a WhateverCode is reported by what it answered.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try { c(); return 'lived' }; $!.^name ~ ': ' ~ $!.message }

my $s = 'abc';
my $start = 'X::OutOfRange: Start argument to substr out of range. Is: ';
my $hint = ' if you want to index relative to the end';
ck msg({ $s.substr(10) }), $start ~ '10, should be in 0..3; use *-10' ~ $hint, 'past the end';
ck msg({ $s.substr(4) }), $start ~ '4, should be in 0..3; use *-4' ~ $hint, 'one past the end';
ck msg({ $s.substr(-1) }), $start ~ '-1, should be in 0..3; use *-1' ~ $hint, 'negative';
ck msg({ $s.substr(-3) }), $start ~ '-3, should be in 0..3; use *-3' ~ $hint, 'back to the start';
ck msg({ $s.substr(-4) }), $start ~ '-4, should be in 0..3; ', 'further back';
ck msg({ $s.substr(-5, 1) }), $start ~ '-5, should be in 0..3; ', 'further back, with a length';
ck msg({ $s.substr(*-5) }), $start ~ '-2, should be in 0..3; use *-2' ~ $hint, 'a WhateverCode';
ck $s.substr(3), '', 'the end itself';

my $len = 'X::OutOfRange: Number of characters argument to substr out of range. Is: ';
ck msg({ $s.substr(1, -1) }), $len ~ '-1, should be in 0..^Inf; use *-1' ~ $hint, 'a negative length';
ck msg({ $s.substr(1, -5) }), $len ~ '-5, should be in 0..^Inf; use *-5' ~ $hint, 'a longer one';
ck msg({ $s.substr(1, *-5) }), $len ~ '-3, should be in 0..^Inf; use *-3' ~ $hint, 'a WhateverCode length';

say 'PASS';
