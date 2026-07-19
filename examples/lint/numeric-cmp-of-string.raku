# Demonstrates: [numeric-cmp-of-string]
# `==` coerces both sides to numbers, so comparing a word with it always fails
# (and warns at run time).  The intended operator is the string `eq`.
# Run:  rakupp --lint numeric-cmp-of-string.raku

sub greet($mood) {
    if $mood == "happy" {          # <-- should be `eq`
        say "🙂";
    }
    else {
        say "😐";
    }
}

greet("happy");
