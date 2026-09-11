# Regression: an attribute trait's argument does not have to be parenthesised.
# `has $.x is column{ :name<y>, :!nullable }` hands the handler a HASH and
# `is column<rootpage>` a word — the two spellings Red writes every model with.
# Only `(…)` was read here, so the brace was left in the token stream, the trait
# arrived as a bare True, no `:%column!` candidate matched, and the whole thing
# silently did nothing: every Red model came out with no columns at all.
#
# `--> Empty` rides along: every one of Red's trait candidates is declared that
# way, and the return type was read as a type name ("Type 'Empty' is not
# declared") rather than as the literal value it is.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my %seen;   # attribute name => what the handler was handed
multi trait_mod:<is>(Attribute $attr, :%mark!      --> Empty) { %seen{$attr.name} = %mark }
multi trait_mod:<is>(Attribute $attr, :$word!      --> Empty) { %seen{$attr.name} = $word }
multi trait_mod:<is>(Attribute $attr, Bool :$flag! --> Empty) { %seen{$attr.name} = $flag }
multi trait_mod:<is>(Attribute $attr, :$paren!     --> Empty) { %seen{$attr.name} = $paren }

class C {
    has $.a is mark{ :one, :two };
    has $.b is word<rootpage>;
    has $.c is flag;
    has $.d is paren('v');
    has $.e;
}

ck(%seen<$!a>.keys.sort.join(','), 'one,two', 'a BRACE argument arrives as a Hash');
ck(%seen<$!b>, 'rootpage',                    'an ANGLE argument arrives as its word');
ck(%seen<$!c>, True,                          'a BARE trait arrives as True');
ck(%seen<$!d>, 'v',                           'and a parenthesised one as its value');
ck(%seen<$!e>:exists, False,                  'an untagged attribute reaches no handler');
ck(C.new(a => 1).a, 1,                        'and the attributes themselves still work');

# --- `--> Empty` on its own -----------------------------------------------
{
    sub e(--> Empty) { 42 }
    ck(e().elems, 0, '`--> Empty` is a literal return, not a type check');
    my @a = 1, e(), 2;
    ck(@a.List, (1, 2), '…and it slips away in a list');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
