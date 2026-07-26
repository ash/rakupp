# Regression: a postfix binds to the CONTEXTUALIZED value, not to the variable
# inside the contextualizer.
#   `@$p[0]` is `(@$p)[0]` — take the list, then subscript it. It parsed as
#   `@($p[0])` — subscript first, then contextualize — so it answered a
#   one-element list instead of the element, and `%$x<a>` answered an empty
#   hash. The circumfix forms (`@(…)[0]`) were always right; only the
#   sigil-on-variable spelling was not, because it parsed its operand with the
#   prefix parser, which consumed the subscript into the operand.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my $pair = ('ok', 'green');
check(@$pair[0].raku,   '"ok"',    'subscript binds outside the list contextualizer');
check(@$pair[1].raku,   '"green"', 'and reaches the second element');
check((@$pair)[0].raku, '"ok"',    'the parenthesised spelling agrees');
check(@($pair)[0].raku, '"ok"',    'and so does the circumfix one');
check(@$pair.elems,     '2',       'without a subscript it is still the whole list');

my @a = 1, 2, 3;
my $r = @a;
check(@$r[1].raku,  '2', 'through an array held in a scalar');
check(@$r[*-1].raku, '3', 'including a from-the-end index');

my %h = a => 1, b => 2;
my $x = %h;
check(%$x<a>.raku, '1', 'the hash contextualizer too');
check(%$x<b>.raku, '2', 'for either key');

# the shape that found this: a list of pairs, subscripted inside a map
my @series = ('ok', 'green'), ('bad', 'red');
check(@series.map({ @$_[0] }).gist, '(ok bad)',    'inside a map block');
check(@series.map({ @$_[1] }).gist, '(green red)', 'for either position');
# the failure mode was a List reaching a Str parameter
sub takes-str(Str $s --> Str) { $s.uc }
check(@series.map({ takes-str(@$_[0]) }).gist, '(OK BAD)', 'and binds to a Str parameter');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
