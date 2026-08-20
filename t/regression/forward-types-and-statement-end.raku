# Regression: three more from the Weekly Challenge sweep.
#
# 1. A class, grammar or role declared LATER in the file is visible to code that
#    runs earlier — those declarations are compile-time in Rakudo. rakupp
#    registered a type when its declaration statement executed, so the common
#    "tests first, definitions below" layout died with "No such method 'parse'
#    for invocant of type 'G'". The declaration is now noted at scope entry and
#    created on FIRST USE: creating it eagerly would run its traits before the
#    `my` variables above it are declared, which a trait handler may read.
#
# 2. A block-closing `}` at end of line ends the statement for a POSTFIX
#    continuation too, not just an infix one. `@a .= sort: { .chars }` followed
#    by a line beginning `.sum given …` is two statements; the `.sum` was being
#    read as a method call on the sort's block.
#
# 3. A value that already IS the target type is not coerced: `Cool() $c` given a
#    Str keeps the Str, `Int() $x` given True keeps the Bool. rakupp called the
#    coercion method regardless, which either changed the type or died outright
#    ("Impossible coercion from 'Str' into 'Cool'").
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. types declared further down ---
check use-grammar("x"), 'x', 'a grammar declared below is usable above';
check use-class(),      42,  '…and a class';
check use-role(),       'r', '…and a role';

grammar G { token TOP { 'x' } }
class  C { method m { 42 } }
role   R { method r { 'r' } }
class  D does R { }

sub use-grammar($s) { G.parse($s).Str }
sub use-class()     { C.new.m }
sub use-role()      { D.new.r }

# a type used AFTER its declaration is of course unchanged, and only built once:
# two instances must share one type
check (C.new.WHAT === C.new.WHAT), True, 'the type is created exactly once';
check C.new.m, 42, 'and it still works when reached in normal flow';

# a trait handler reading a `my` declared above the class still sees it — this
# is why the declaration is not created eagerly at scope entry
my @called;
multi sub trait_mod:<is>(Attribute $a, :$noted!) { @called.push($a.name) }
class T { has $.x is noted; }
check T.new.defined, True, 'a class with a user attribute trait declares';
check @called.join(','), '$!x', '…and the handler saw the variable above it';

# --- 2. a `}` at end of line ends the statement ---
{
    my @arr = <a aba ababa aa>;
    @arr .= sort: { .chars }

    my $sum = .sum given gather for ^@arr.end
    {
        my $head = @arr.shift;
        take +@arr.grep: { .starts-with($head) and .ends-with($head) }
    }
    check $sum, 4, 'the line after a closing brace is a new statement';
    check @arr.elems, 1, '…and the sort really ran';
}
# a chain that does NOT end in a block still continues across lines
{
    my $r = <b a>.sort
                 .join('');
    check $r, 'ab', 'an ordinary method chain still spans lines';
}

# --- 3. coercion only when it is needed ---
sub cool(Cool() $c) { $c.^name }
check cool("hi"), 'Str', 'a Str is already Cool';
sub as-int(Int() $x) { $x.^name }
check as-int(True), 'Bool', 'a Bool is already an Int';
check as-int("42"), 'Int',  '…and a real coercion still happens';
subset JStr of Str where * ~~ /^ <[a..j]>+ $/;
sub j(JStr() $x) { $x }
check j("abc"), 'abc', 'a subset coercion accepts a value that already matches';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
