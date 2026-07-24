# Regression: general `T(x)` type-coercion calls — a known type used as a routine
# coerces its argument through the argument's `.T` method (Raku's coercion
# protocol), instead of failing with "Undefined routine 'T'" for types outside
# the Int/Str/... fast-path set.
# Contract: exit 0 + last line PASS.
my @fail;

# the specialized coercers still route correctly
@fail.push('int-coerce') unless Int('42') == 42;
@fail.push('str-coerce') unless Str(42) eq '42';
@fail.push('num-coerce') unless Num('1.5') == 1.5e0;

# Supply($channel) coercion == $channel.Supply (a type outside the fast set)
my $c = Channel.new; $c.send(7); $c.send(8); $c.close;
@fail.push('supply-coerce') unless Supply($c).list eqv (7, 8);

# a user coercion method: `MyT($x)` == `$x.MyT`
class Celsius { has $.c; method Str { "{$!c}C" } }
@fail.push('str-of-obj') unless Str(Celsius.new(c => 20)) eq '20C';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
