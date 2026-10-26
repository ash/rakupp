# race-detect: report
# location: hash entry %h<k>
#
# Two workers store into one key that already exists. No insert, so the
# shape is only read; the entry is written by both.
my %h = k => 0;
await (^2).map: -> $w { start { %h<k> = $w for ^2_000 } };
say %h<k> ~~ 0 | 1;
