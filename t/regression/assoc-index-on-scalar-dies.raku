# Regression: `$str<key>` answered an empty Any instead of dying. Rakudo raises
# "Type Str does not support associative indexing." for any DEFINED
# non-Associative value; rakupp had the check for Array only, so every other
# type fell through to the Hash miss path and returned Any.
#
# Silence is the wrong failure here, because `<…>` after a variable is not
# always something the author typed on purpose: in a qq template
# `"<div>$a$b</div>"` the parser reads `$b</div>` as `$b<'/div'>`, and the
# empty answer swallows the closing tag along with it. That is exactly how a
# generated page went out with its markup quietly truncated — under Rakudo the
# same template dies on the spot.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub dies-with($desc, &code) {
    my $msg = '';
    { code(); CATCH { default { $msg = .Str } } }
    @fail.push("$desc: no die") unless $msg;
    @fail.push("$desc: wrong message ($msg)")
        if $msg && !$msg.contains('does not support associative indexing');
    $msg
}

# defined non-Associative values die, and name their own type
my $s = "abc";
my $i = 42;
my $b = True;
my $r = 1/2;
@fail.push('Str names its type')  unless dies-with('Str',  { $s<k> }).contains('Type Str');
@fail.push('Int names its type')  unless dies-with('Int',  { $i<k> }).contains('Type Int');
@fail.push('Bool names its type') unless dies-with('Bool', { $b<k> }).contains('Type Bool');
@fail.push('Rat names its type') unless dies-with('Rat',  { $r<k> }).contains('Type Rat');
dies-with('Array', { my @a = 1, 2; @a.item<k> });

# the interpolation that started it: `$b</div>` is a subscript, and it dies
my $x = 'A';
my $y = 'B';
dies-with('interpolated subscript', { my $t = "<div>$x$y</div>"; $t });

# ...while the spelling that MEANS concatenation is untouched
@fail.push('braced interpolation broke') unless "<div>{$x}{$y}</div>" eq '<div>AB</div>';

# undefined values and type objects stay quiet, as they do under Rakudo
my $undef;
@fail.push('undefined should stay Any') unless $undef<k>.^name eq 'Any';
@fail.push('type object should stay Any') unless Str<k>.^name eq 'Any';

# and the things that ARE associative still work
my %h = a => 1;
@fail.push('hash subscript broke') unless %h<a> == 1;
my $href = %h;
@fail.push('hash-in-scalar subscript broke') unless $href<a> == 1;
my $pair = (b => 2);
@fail.push('pair subscript broke') unless $pair<b> == 2;
@fail.push('match subscript broke') unless ('ab' ~~ / $<first>=[.] /)<first> eq 'a';

die @fail.join('; ') if @fail;
say "PASS";
