# A Date or DateTime field given as a Str that is no number is refused as
# Rakudo refuses it: `Date.new(2015, "x", 1)` was "Month out of range. Is:
# 0", a year "x" was 0, an hour "x" midnight and a second "x" kept as "x",
# where Rakudo dies unboxing it (X::AdHoc) or, for the second, says it is
# out of range. A timezone Str is read as a field's beside fields, and
# refused whole beside a timestamp, a POSIX time or `.now`. A numeric Str is
# its number, as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg($code) { (try { EVAL $code; 1 }) // "{$!.^name}: {$!.message}" }
my $unbox = 'X::AdHoc: This type cannot unbox to a native integer: P6opaque, Failure';

ck msg(q[Date.new(2015, "x", 1)]), $unbox, 'a month';
ck msg(q[Date.new("x", 1, 1)]), $unbox, 'a year';
ck msg(q[Date.new(2015, 1, "x")]), $unbox, 'a day';
ck msg(q[Date.new(:year("x"), :month(1), :day(1))]), $unbox, 'a named one';
ck msg(q[DateTime.new(2015, "x", 1, 0, 0, 0)]), $unbox, "a DateTime's month";
ck msg(q[DateTime.new(2015, 1, 1, "x", 0, 0)]),
   q[X::AdHoc: P6opaque: get_boxed_ref could not unbox for the representation 'P6bigint' of type Failure], 'an hour';
ck msg(q[DateTime.new(2015, 1, 1, 0, "x", 0)]),
   q[X::AdHoc: P6opaque: get_boxed_ref could not unbox for the representation 'P6bigint' of type Failure], 'a minute';
ck msg(q[DateTime.new(2015, 1, 1, 0, 0, "x")]), q[X::OutOfRange: Second out of range. Is: "x", should be in ^61], 'a second';
ck msg(q[DateTime.new(2015, 1, 1, 0, 0, 0, :timezone("x"))]), $unbox, 'a timezone beside fields';
ck msg(q[DateTime.new("2012-12-22T07:02:00", :timezone("3600"))]),
   'X::AdHoc: This type cannot unbox to a native integer: P6opaque, Str', 'beside a timestamp, any Str';
ck msg(q[DateTime.new(0, :timezone("60"))]), 'X::AdHoc: This type cannot unbox to a native integer: P6opaque, Str', 'a POSIX time';
ck msg(q[DateTime.now(:timezone("60"))]), 'X::AdHoc: This type cannot unbox to a native integer: P6opaque, Str', '.now';

ck (Date.new(2015, "3", 1), DateTime.new(2015, 1, 1, 0, 0, "1.5").second, DateTime.new(:year(2015), :timezone("60")).timezone,
    DateTime.new(Instant.from-posix(0), :timezone("60")).minute),
   (Date.new(2015, 3, 1), "1.5", 60, 1), 'a number in a Str, as before (a second kept as given)';
ck msg(q[Date.new(2015, "", 1)]), 'X::Temporal::OutOfRange: Month out of range. Is: 0, should be in 1..12', 'an empty one is 0';

say 'PASS';
