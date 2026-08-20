# Regression: four divergences from the Weekly Challenge sweep.
#
# 1. Whitespace inside a character class is insignificant, RANGES included:
#    `<[ a .. z ]>` is `<[a..z]>`. rakupp looked for the `..` immediately after
#    the low endpoint, so the spaced-out spelling became three literal members
#    (a, ., z) and `Str $s where m:i/ ^ <[ a .. z ]>* $ /` rejected every word.
#
# 2. `≡` is set equality — `(==)` — not `===`: it compares its operands AS
#    SETS, so `(1,2) ≡ (2,1)` is True. rakupp lexed it to `===` (value
#    identity), and `cmp-ok $a, '≡', $b` — where the operator arrives as a
#    STRING and never passes the lexer — died with "Unsupported operator".
#
# 3. A `{ … }` mentioning `@_` or `%_` is a BLOCK, exactly as one mentioning
#    `$_` is. `.map: { @_[0] => @_[1] }` was read as a Hash literal, so the map
#    produced nothing at all.
#
# 4. `for` aliases the elements of an ARRAY named in its list, whether spelled
#    `@a` or slipped as `|@a`. Only bare scalars were aliased, so the classic
#    trim-everything loop silently left the array members untrimmed.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. spaces around .. in a character class ---
check ?("m" ~~ m/<[ a .. z ]>/),  True,  'a spaced range is still a range';
check ?("." ~~ m/<[ a .. z ]>/),  False, '…and the dots are not members';
check ?("B" ~~ m:i/<[ a .. z ]>/), True, '…under :i too';
check ("bbc" ~~ m/ ^ <[ a .. z ]>* $ /).Str, 'bbc', '…anchored, as a where-clause writes it';
check ?("A" ~~ m/<[ \x41 .. \x5A ]>/), True, '…with escaped endpoints';
check ?("5" ~~ m/<[ 0..9 a..f ]>/), True, '…and two ranges in one class';
# an unspaced `.` is still a literal member
check ?("." ~~ m/<[a . z]>/), True, 'a lone dot remains a literal member';
sub w(Str:D $s where m:i/ ^ <[ a .. z ]>* $ /) { $s }
check w("Bbc"), 'Bbc', 'the where-clause that started this accepts its argument';

# --- 2. the set identity glyphs ---
check ((1,2) ≡ (2,1)),  True,  '≡ compares as sets, so order does not matter';
check ((1,1,2) ≡ (1,2)), True, '…nor do repeats';
check ((1,2) ≢ (2,1)),  False, '…and ≢ is its negation';
check ([1,2] ≡ [1,2]),  True,  'two distinct Arrays are set-equal';
check (bag(1,2) ≡ bag(1,2)), True, 'and bags compare by their counts';
{
    use Test;
    my $ok = cmp-ok (1,2).Bag, '≡', (1,2).Bag, 'cmp-ok takes the glyph as a string';
    @fail.push('cmp-ok with ≡ failed') unless $ok;
}

# --- 3. @_ makes it a block ---
my @D = <a b>;
check (@D Z 0 .. @D.end).map({ @_[0] => @_[1] }).map(*.gist).join(' '), 'a => 0 b => 1',
      'a composer mentioning @_ is a block';
check ((@D Z 0 .. @D.end).map: { @_[0] => @_[1] }).map(*.gist).join(' '), 'a => 0 b => 1',
      '…in the colon-call spelling too';
check %( a => 1, b => 2 ).elems, 2, 'a real hash literal still composes';
check ({ 3 => 4, :b }).^name, 'Hash', '…and one with no topic in it stays a Hash';

# --- 4. for aliases array members ---
my @a = "x ", "y ";
my $s = "z ";
for $s, |@a { s/ \s+ $ // }
check @a.raku, '["x", "y"]', 'a slipped array is aliased';
check $s, 'z',               '…alongside the scalar next to it';
my @c = "m ", "n ";
for |@c { s/ \s+ $ // }
check @c.raku, '["m", "n"]', 'a lone slipped array too';
my @b = "p ", "q ";
for @b { s/ \s+ $ // }
check @b.raku, '["p", "q"]', 'and the plain form still works';
my @d = 1, 2;
my @seen;
for @d, 3 { @seen.push($_) }
check @seen.join(' '), '1 2 3', 'a non-aliasable member does not break the loop';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
