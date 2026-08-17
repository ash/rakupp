# BROKE: `rakupp fft.raku` (the Rosetta Cooley–Tukey FFT) died
# "Too many levels of recursion". Two cooperating gaps:
#   * `@a[0, 2 ... *]` flattened the infinite index Seq to its two seeds,
#     so the even/odd split never shrank; a 2-element list sliced to 2
#     again and fft recursed until the guard fired. Rakudo stops a lazy
#     slice at the first hole, so the split is (n, n/2, …, 1).
#   * `map &cis, (0, -tau/n ... *)` used the same seed-only toList, so
#     the twiddle `Z*` was short and `»+«` then died on a length mismatch.
# FIXED: a lazy Seq used as a positional subscript is pulled until the
# first out-of-range index; map-the-sub delegates a lazy list to .map.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my @a = 10, 20, 30, 40, 50, 60, 70, 80;
check(@a[0, 2 ... *].join(','), '10,30,50,70', 'even slice of 8');
check(@a[1, 3 ... *].join(','), '20,40,60,80', 'odd slice of 8');
check(@a[2, 4 ... *].join(','), '30,50,70',    'even tail of 8');

my @two = 10, 20;
check(@two[0, 2 ... *].join(','), '10', 'even slice of 2 shrinks to 1');
check(@two[1, 3 ... *].join(','), '20', 'odd slice of 2 shrinks to 1');
check((42,)[0, 2 ... *].join(','), '42', 'even slice of 1 is the element');
check(@a[10, 12 ... *].elems, 0, 'a slice that starts past the end is empty');

# stored Seq is the same as the inline form
my $idx = (0, 2 ... *);
check(@a[$idx].join(','), '10,30,50,70', 'stored infinite Seq as a slice');

# map-the-sub over an infinite arithmetic Seq stays lazy (Z* pulls what it needs)
check(((1, 1, 0, 0) Z* map { $_ }, (0, -1 ... *)).join(','),
      '0,-1,0,0', 'map over …* then Z* against a finite left side');
check((map &cis, (0, -tau / 8 ... *)).head(2).elems, 2, 'map &cis of a Num …* Seq');

# the fft recursion shape: each even/odd split must get strictly smaller
sub evens(@x) { @x[0, 2 ... *] }
my @sizes;
my @cur = @a;
loop {
    @sizes.push(@cur.elems);
    last if @cur == 1;
    @cur = evens(@cur);
}
check(@sizes.join(','), '8,4,2,1', 'repeated even-slice reaches 1');

# the original program: Cooley–Tukey over 8 samples
sub fft {
    return @_ if @_ == 1;
    my @evn = fft( @_[0, 2 ... *] );
    my @odd = fft( @_[1, 3 ... *] ) Z*
    map &cis, (0, -tau / @_ ... *);
    return flat @evn »+« @odd, @evn »-« @odd;
}
check(fft(<1 1 1 1 0 0 0 0>).join(';'),
      '4+0i;1-2.414213562373095i;0+0i;1-0.4142135623730949i;0+0i;0.9999999999999999+0.4142135623730949i;0+0i;0.9999999999999997+2.414213562373095i',
      'fft of <1 1 1 1 0 0 0 0>');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL'; exit 1 } else { say 'PASS' }
