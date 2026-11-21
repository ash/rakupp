# Regression (#276): `Duration(120)` and the other built-in types that no value
# has a coercion METHOD for.
#
# `T(x)` on a built-in type was answered by calling `x.T`, and there is no
# `.Duration`, `.Blob`, `.Buf` or `.Range` on any value: `Duration(120)` died
# with "No such method 'Duration' for invocant of type 'Int'", and so did
# `Blob(1, 2)`, `buf8(1, 2)`, `Range(1..3)`, `Duration($a-duration)` and
# `IO::Path("x")`. Rakudo builds a Duration from the value's Real and a Blob
# from its elements, and a value already of the type is itself. The same
# coercion through a `Duration()` parameter or a type held in a variable said
# "Impossible coercion"; `Blob[uint16](1, 2)` kept one byte and lost the width.
# Every answer here is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

check(Duration(120).Str,               '120',        'Duration(Int)');
check(Duration(120).^name,             'Duration',   'is a Duration');
check(Duration(1/2).^name,             'Duration',   'Duration(Rat)');
check(Duration("3.5").Str,             '3.5',        'Duration(Str) numifies');
check((try Duration("x")) // 'died',   'died',       'Duration of a non-number dies');
check(Duration(Duration.new(2)).^name, 'Duration',   'a Duration is itself');
check(Duration().raku,                 'Duration(Any)', 'no argument: the coercion type');
sub dur(Duration() $d) { $d.^name }
check(dur(3),                          'Duration',   'a Duration() parameter');
my $dt = Duration;
check($dt(5).^name,                    'Duration',   'a type in a variable');

check(Blob(1, 2).raku,                 'Blob.new(1,2)', 'Blob(elements)');
check(Buf(1, 2).raku,                  'Buf.new(1,2)',  'Buf(elements)');
check(Blob((1, 2)).raku,               'Blob.new(1,2)', 'Blob(a list)');
check(Buf(Blob.new(1)).^name,          'Buf',        'Buf(a Blob)');
check(Blob(Buf.new(1)).^name,          'Buf',        'a Buf is already a Blob');
check(buf8(1, 2).gist,                 'Buf[uint8]:0x<01 02>',  'buf8(elements)');
check(blob8(3).gist,                   'Blob[uint8]:0x<03>',    'blob8(an element)');
check(Blob[uint16](1, 2).gist,         'Blob[uint16]:0x<0001 0002>', 'a parameterized Blob keeps its width');
my $bt = Blob;
check($bt(1, 2).raku,                  'Blob.new(1,2)', 'a Blob type in a variable takes every element');
sub blb(Blob() $b) { $b.raku }
check(blb((1, 2)),                     'Blob.new(1,2)', 'a Blob() parameter');

check(Range(1..3).Str,                 '1 2 3',      'a Range is itself');
check((try Range("a")) // 'died',      'died',       'Range of a Str is an impossible coercion');
check(IO::Path("x").^name,             'IO::Path',   'IO::Path(Str)');
check(Instant(Instant.from-posix(5)).^name, 'Instant', 'an Instant is itself');

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
