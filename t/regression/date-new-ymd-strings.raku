# BROKE: leg 21 — X::Temporal::InvalidFormat for non-ISO date strings also
# rejected the multi-positional string form Date.new('2018', '1', '4'),
# which is legal (numeric strings coerce to y/m/d; S32-temporal/Date.t -1,
# caught in spot-checks before the gate).
# FIXED: same leg — the check requires a SINGLE string positional.
die 'Date.new(y,m,d strings) broken' unless Date.new('2018', '1', '4') eq '2018-01-04';
die 'Date.new(bad string) must still throw' unless (try Date.new("2012/04")) === Nil;
say 'PASS';
