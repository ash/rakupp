# Regression: Setty/Baggy .Str is the elements space-joined — `elem(weight)`
# for a non-1 Bag/Mix weight — not the generic Hash key\tvalue dump.
# Found via Weekly Challenge 385 task 1: `say ([(^)] @case.words).Str`
# printed "orange\tTrue" instead of "orange". (.gist was already correct.)
# Element ORDER is unordered in Rakudo (per-run hash order); rakupp renders
# sorted, so the checks here are order-insensitive where more than one
# element is involved. Contract: exit 0 + last line PASS.
my @fail;

@fail.push('set-str') unless (set <b a c>).Str.words.sort.join(' ') eq 'a b c';
@fail.push('set-str-tab') if (set <a>).Str.contains("\t");
@fail.push('sethash-str') unless SetHash.new(<x y>).Str.words.sort.join(' ') eq 'x y';
@fail.push('bag-str') unless (bag <a a b>).Str.words.sort.join(' ') eq 'a(2) b';
@fail.push('mix-str') unless (a => 0.5, b => 2).Mix.Str.words.sort.join(' ') eq 'a(0.5) b(2)';
@fail.push('mix-one-bare') unless (a => 1, b => 2).Mix.Str.words.sort.join(' ') eq 'a b(2)';
@fail.push('gist-unchanged') unless (set <a>).gist eq 'Set(a)';

# the challenge shape end-to-end: reduce (^) over ONE flat word list —
# each word an operand, so only words appearing in exactly one place survive
my $r = ([(^)] "blue blue red red green green yellow".words).Str;
@fail.push("challenge ($r)") unless $r eq 'yellow';

# …while COMMA items are each ONE operand: the symmetric difference of two
# word LISTS is presence-in-exactly-one (blue is in the first list only)
my $two = ([(^)] "blue blue red".words, "red green green yellow".words).Str;
@fail.push("two-lists ($two)") unless $two.words.sort.join(' ') eq 'blue green yellow';

if @fail {
    say "FAILED: @fail[]";
    say "FAIL";
    exit 1;
}
say "PASS";
