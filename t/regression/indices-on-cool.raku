# BROKE: `.indices` took only a Str or a Match: `422.indices(2)` was "No such
# method 'indices' for invocant of type 'Int'", where Rakudo, as for `.index`,
# reads the Cool as its Str and gives (1, 2). S32-str/indices.t died there; it
# read as passing, the harness taking a run that printed no plan for a pass
# (found 2026-10-23). Past it, `42.indices: Str` answered (0 1 2), the type
# read as "", where Rakudo dies.
# FIXED: `.indices` takes a numeric Cool or a Bool as its Str and refuses a
# type-object needle as `.index` does (src/MethodCallTail.cpp).
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

ck(422.indices(2), (1, 2), '422.indices(2)');
ck(indices(422, 2), (1, 2), 'indices(422, 2)');
ck(42424242.indices(4, 2), (2, 4, 6), 'with a start position');
ck(1.5.indices('5'), (2,), 'a Rat');
ck(True.indices('u'), (2,), 'a Bool');
ck(<42>.indices(2), (1,), 'an IntStr');
# (Rakudo's error is X::Multi::NoMatch, Raku++'s for `.index` too a binding one)
ck((try 42.indices(Str)) // $! ~~ Exception, True, 'a type object is no needle');
ck((try 'abc'.indices(Str)) // $! ~~ Exception, True, '…on a Str either');

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
