# Regression: four general fixes found via the Tier-2 module battery (v2,
# 2026-07-23). Each fixed a real ecosystem module.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. a Blob/Buf iterates its BYTES in `for` (MIME::Base64 reads the buffer
#    3-at-a-time via `for $data -> $b1,$b2?,$b3?`)
{
    my @b;
    sub eat(Blob:D $d) { for $d -> $x { @b.push($x) } }
    eat("hi".encode('utf8'));
    ck(@b, [104, 105], 'Blob param iterates bytes');
}

# 2. an allomorph binds to a native `str` param and `str @` array (its Str side)
{
    sub s(str $x) { $x }
    ck(s(<8>).Str, "8", 'IntStr binds to str param (stringifies)');
    my str @a; @a.push(<9>);
    ck(@a[0].Str, "9", 'IntStr pushes to str array (stringifies)');
}

# 3. a Regex in boolean context matches $_ (URI::Encode tests each char)
{
    my @hit;
    for "a!b".comb { my $r := rx/<[a..z]>/; @hit.push($_) if $r }
    ck(@hit, ["a", "b"], 'Regex in boolean context matches $_');
}

# 4. `:16(...)` is a radix literal, so `{ :16($_) }` is a CODE block, not a hash
{
    ck({ :16($_).Int }("ff"), 255, ':16($_) in a block is radix');
    ck((map { :16($_).Int }, "ff00".comb(/../)), (255, 0), ':16 map');
    my %h = :a(1); ck(%h<a>, 1, 'real hash literal still works');
}

say 'PASS' if $ok;
