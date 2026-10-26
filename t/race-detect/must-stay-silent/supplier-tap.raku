# race-detect: silent
# edges: supply
# expect: 2000 1999000
#
# t/stress/supply-fanin.raku's shape: four workers emit into one Supplier and
# a single tap counts. A tap's runs are serialized (the Supplier's mutex), so
# each run is ordered after the one before it, whichever thread ran it.
my $s = Supplier.new;
my ($count, $sum) = 0, 0;
$s.Supply.tap({ $count++; $sum += $_ });
await (^4).map: -> $w { start { $s.emit($w * 500 + $_) for ^500 } };
say "$count $sum";
