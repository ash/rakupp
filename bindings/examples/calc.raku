# The second shared example: Raku as a library, with no grammar in sight.
# Where shopping.raku shows the parsing story, this one shows the general
# one — a host evaluates Raku source, calls these subs with its OWN values
# (numbers, lists, maps), and reads the results back as its own types.
#
# Nothing here is special. They are ordinary subs in the interpreter's
# mainline scope, which is exactly what `eval` puts there and what `call`
# looks up by name.

# Scalars in, scalar out.
sub area($w, $h) { $w * $h }

# A host list arrives as a Raku list. `mean` is formatted HERE, in Raku,
# because a Rat printed by five different languages is five different
# strings — when the exact text matters, decide it on this side.
sub stats(@n) {
    %(
        count => @n.elems,
        sum   => @n.sum,
        mean  => (@n.sum / @n.elems).fmt('%.2f'),
        max   => @n.max,
    )
}

# A host map arrives as a Raku hash.
sub greet(%who) { "Hello, %who<name>! You are %who<age>." }

# A Raku list comes back as the host's own list type.
sub primes-below($n) { (2 ..^ $n).grep(*.is-prime).List }

# Raku integers do not overflow. Past 64 bits the host cannot hold the
# number, so this returns the digits as a string — the honest conversion.
sub factorial($n) { ([*] 1..$n).Str }

# Errors cross the boundary too: this die becomes the host's own exception
# type — RakuError, ParseError's sibling.
sub checked-div($a, $b) {
    die "division by zero" if $b == 0;
    $a / $b
}
