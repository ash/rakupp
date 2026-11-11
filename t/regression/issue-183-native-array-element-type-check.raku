# Regression: issue #183 (2026-11-10).
#
# `my int @array = 1, 2, 3; @array[1] = 'foo'` stored the Str, and .raku
# said array[int].new(1, "foo", 3). An element store into a native array is
# type-checked, from a '...' StrLit as from a "..." InterpStr.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}
sub refused(&code) { my $r = try { code(); 'stored' }; $r // $!.^name }

my int @array = 1, 2, 3;
check refused({ @array[1] = 'foo' }), 'X::TypeCheck', "int element = 'foo'";
check refused({ @array[1] = "foo" }), 'X::TypeCheck', 'int element = "foo"';
my $v = 'bar';
check refused({ @array[2] = "$v" }), 'X::TypeCheck', 'int element = "$v"';
check refused({ @array[0] = $v }), 'X::TypeCheck', 'int element = a Str variable';
check @array.raku, 'array[int].new(1, 2, 3)', '...the int array is as it was';
my $e = (try { @array[1] = 'foo' }) // $!;
check $e.message, 'Type check failed in assignment to int array element #1; expected int but got Str ("foo")', 'the message';

sub in-routine() { my int @a = 1, 2; try { @a[0] = 'x' }; ($!.^name, @a.List) }
check in-routine(), ('X::TypeCheck', (1, 2)), 'inside a routine';

my num @n = 1e0, 2e0;
check refused({ @n[0] = 'x' }), 'X::TypeCheck', "num element = 'x'";
check @n.raku, 'array[num].new(1e0, 2e0)', '...the num array is as it was';
my str @s = 'a', 'b';
check refused({ @s[0] = 5 }), 'X::TypeCheck', 'str element = 5';
check @s.raku, 'array[str].new("a", "b")', '...the str array is as it was';

my Int @typed = 1, 2, 3;
check refused({ @typed[1] = 'foo' }), 'X::TypeCheck::Assignment', "Int @ element = 'foo'";

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
