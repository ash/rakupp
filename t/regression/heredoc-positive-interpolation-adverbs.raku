# Regression: positive interpolation adverbs on a `q`/`Q` heredoc are ADDITIVE.
# `q:s:to/END/` interpolates scalars and nothing else; `Q:c:s:to/END/` adds
# closures too. Only `qq:to` turned interpolation on, so these heredocs came
# out as their own source text — FEN::Result.show-state printed the literal
# `$.active-color`, and Image::Markup::Utilities emitted its whole SVG
# template verbatim instead of an animation.
#
# This is the mirror of the `:!x` subtraction that already worked
# (`qq:!c:to/END/` interpolates variables but not blocks).
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $name  = 'Ada';
my @parts = <a b>;
my %h     = k => 'v';

ck(q:s:to/END/.chomp, 'scalar Ada', ':s interpolates a scalar');
scalar $name
END

ck(q:s:to/END/.chomp, 'braces {1 + 1} stay', ':s alone leaves closures alone');
braces {1 + 1} stay
END

ck(Q:c:s:to/END/.chomp, 'Ada and 2', ':c:s does both');
$name and {1 + 1}
END

ck(q:a:to/END/.chomp, 'array a b', ':a interpolates an array');
array @parts[]
END

ck(q:to/END/.chomp, 'plain $name stays', 'a bare q:to interpolates nothing');
plain $name stays
END

ck(qq:to/END/.chomp, 'qq Ada 2', 'qq:to still does everything');
qq $name {1 + 1}
END

ck(qq:!c:to/END/.chomp, 'nc Ada {1 + 1}', 'and :!c still subtracts');
nc $name {1 + 1}
END

# the shape the two modules use: a template with fields of an object
class Position {
    has $.colour = 'White';
    has $.moves  = 7;
    method show { q:s:to/SHOW/ }
        Active colour: $.colour
        Moves: $.moves
        SHOW
}
ck(Position.new.show.lines.map(*.trim).join('|'),
   'Active colour: White|Moves: 7',
   'an object renders its own fields through a q:s heredoc');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
