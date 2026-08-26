# Issue #34, second parse error in the same file: TAP line 1051 is
# `await Promise.anyof(@working»<done>, $bailout)` — a HYPER HASH subscript.
#
# `»[0]`, `»**2`, `».method` and `».<key>` all parsed; the two postcircumfix
# spellings `»<key>` and `»{EXPR}` did not, and died "expected ) (got '»')".
# The hyper INFIX forms (`@a »<« @b`) are lexed as ONE operator token, so a bare
# marker followed by a tight `<`/`{` can only be the subscript.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my @h = { :d(1), :e(10) }, { :d(2), :e(20) };

check (@h»<d>).List,      (1, 2),   '@h»<d>';
check (@h>><d>).List,     (1, 2),   '@h>><d> (ASCII marker)';
check (@h»{"d"}).List,    (1, 2),   '@h»{"d"}';
check (@h>>{"d"}).List,   (1, 2),   '@h>>{"d"}';
check (@h»<d e>).List,    ((1, 10), (2, 20)), '@h»<d e> slices each element';
my $k = 'e';
check (@h»{$k}).List,     (10, 20), '@h»{$k} — a computed key';

# chained, and mixed with the spellings that already worked
check (@h»<d>».Str).List, ("1", "2"), '@h»<d>».Str chains onto a hyper subscript';
check (@h».<d>).List,     (1, 2),   '@h».<d> (dotted form) still works';

my @aoa = (1, 2), (3, 4);
check (@aoa»[0]).List,    (1, 3),   '@aoa»[0] (positional hyper) still works';

# …and the hyper INFIXES the new `<`/`{` cases must not have stolen
my @a = 1, 2;
my @b = 3, 4;
check (@a »<« @b).List,   (True, True),  '@a »<« @b is still hyper less-than';
check (@a >>+<< @b).List, (4, 6),        '@a >>+<< @b is still hyper plus';
check (@a >>[&infix:<+>]<< @b).List, (4, 6), 'the bracket-op hyper infix still parses';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
