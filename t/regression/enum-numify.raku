# Regression: an ENUM VALUE numifies to a PLAIN Int.
#
# `.Numeric` and prefix `+` returned the value UNCHANGED when it was already
# numeric — which for an enum value meant handing back something still carrying
# its enumName, so it rendered as `b` rather than 1. `.Int` and `.value` build a
# fresh Int and were right all along, so one value answered three ways.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

enum E <a b c>;
check(b.Numeric.gist, '1', '.Numeric on an enum value is a plain Int');
check(b.Int.gist,     '1', '.Int agrees');
check(b.value.gist,   '1', '.value agrees');
check((+b).gist,      '1', 'prefix + agrees');
check(b.gist,         'b', 'the value itself still gists as its name');
check((b.Numeric + 1).gist, '2', 'the numified value does arithmetic');

enum Endian (NativeEndian => 0, LittleEndian => 1);
check(LittleEndian.Numeric.gist, '1', 'an explicitly-valued enum too');
check(NativeEndian.Numeric.gist, '0', 'including zero');

# a plain number is still ITSELF, not forced through Num — the reason the
# `return inv` shortcut existed in the first place
check(3.Numeric.gist,      '3',      'an Int stays an Int');
check((-4/3).Real.raku,    '<-4/3>', 'a Rat stays a Rat');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
