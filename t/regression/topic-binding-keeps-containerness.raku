# Regression: `given`/`with` BIND $_, so the topic keeps its source's
# container-ness instead of taking one from $_'s sigil. Found by the full
# ecosystem sweep (Data::Transformers): `given @data { for $_ -> @row { … } }`
# iterated ONCE, handing the whole matrix to @row, because `for $_` treated
# any $-sigil source as a single item. A named scalar really is one item —
# assignment into a Scalar container itemizes — but $_ holds whatever it was
# bound to, and only the value's own itemized flag can say which it was.
#
# Oracle-verified against Rakudo 2026.07, shape by shape.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# an @-array topic is a bare Iterable: for $_ iterates its elements
my @a = 1, 2, 3;
given @a { my $n = 0; $n++ for $_; ok($n == 3, 'given @a: for $_ iterates the elements') }
with  @a { my $n = 0; $n++ for $_; ok($n == 3, 'with @a: same binding rule') }

# a $-scalar topic is a container: for $_ is ONE topic
my $x = [1, 2];
given $x { my $n = 0; $n++ for $_; ok($n == 1, 'given $x: one topic') }

# a literal Array is bare, a %-hash iterates pairs, a $-held hash does not
given [1, 2]      { my $n = 0; $n++ for $_; ok($n == 2, 'given [1,2]: bare Array iterates') }
my %h = a => 1, b => 2;
given %h { my $n = 0; $n++ for $_; ok($n == 2, 'given %h: iterates the pairs') }
my $hh = { a => 1, b => 2 };
given $hh { my $n = 0; $n++ for $_; ok($n == 1, 'given $hh: one topic') }

# re-topicalizing passes the current topic through, whatever its provenance
given @a { given $_ { my $n = 0; $n++ for $_; ok($n == 3, 'given $_ keeps a bare topic bare') } }

# for's own $_ still follows the element rule: Array elements sit in fresh
# scalar containers (one topic each), List elements are bare
my @t = (1, 2), (3, 4);
my @inner-array;
for @t { my $n = 0; $n++ for $_; @inner-array.push($n) }
ok(@inner-array.join(',') eq '1,1', 'for @t: elements arrive itemized');
my @inner-list;
for ((1, 2), (3, 4)) { my $n = 0; $n++ for $_; @inner-list.push($n) }
ok(@inner-list.join(',') eq '2,2', 'for (List of Lists): elements arrive bare');

# the sweep's shape: per-row accumulation over a matrix
my @m = [10, 7], [2, 8], [0, 2];
my @res;
given @m {
    for $_ -> @row {
        if @res {
            @res.push((@res.tail <<+>> @row).Array);
        }
        else {
            @res.push(@row.Array);
        }
    }
}
ok(@res.raku eq '[[10, 7], [12, 15], [12, 17]]', 'given a matrix, for $_ walks its rows');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
