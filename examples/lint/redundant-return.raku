# Demonstrates: [redundant-return]  (a NOTE — a style nudge, not a bug)
# A block's value is its last expression, so the trailing `return` adds nothing.
# Run:  rakupp --lint redundant-return.raku

sub area($w, $h) {
    my $a = $w * $h;
    return $a;          # <-- the last value is returned automatically
}

say area(3, 4);
