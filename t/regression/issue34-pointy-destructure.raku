# Issue #34: `rakupp install TAP` died with "expected { (got '(')".
#
# TAP line 457 is `if $result.errors -> @ ($head, *@tail) { … }` — a pointy
# binder on a STATEMENT condition carrying a destructuring sub-signature.
# `if`/`elsif`/`else` parsed their binder with a hand-rolled reader that only
# took a bare name, so any signature after `->` died at the block brace; `while`
# took only the `-> (…)` spelling; and `given`/`with`/`without` silently bound
# NOTHING at all (the block then saw undeclared variables). All of them now
# parse the full pointy-signature grammar and bind through bindParams.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my @e = 1, 2, 3;

# --- the shape from TAP: an anonymous @ with a sub-signature -----------------
if @e -> @ ($head, *@tail) { check $head, 1, 'if -> @ ($head, *@tail): head' ;
                             check @tail.List, (2, 3), 'if -> @ ($head, *@tail): tail' }
else                       { @fail.push('if -> @ (…) did not run') }

# --- every other spelling of the same binder ---------------------------------
if @e -> ($a, $b, $c)      { check ($a, $b, $c), (1, 2, 3), 'if -> ($a,$b,$c)' }
if @e -> $ ($h, *@t)       { check $h, 1, 'if -> $ ($h, *@t)' }
if @e -> @all ($h, *@t)    { check (@all.elems, $h), (3, 1), 'if -> @all ($h,*@t) binds both' }
if @e -> @ [$h, *@t]       { check $h, 1, 'if -> @ [$h, *@t] (bracket sub-signature)' }

# elsif, and else (which binds the last, falsy, condition value)
if 0 { @fail.push('elsif: wrong branch') }
elsif @e -> ($a, $b, $c) { check $c, 3, 'elsif -> ($a,$b,$c)' }

my @empty;
if @empty -> ($x, $y) { @fail.push('else: wrong branch') }
else -> @ (*@rest) { check @rest.elems, 0, 'else -> @ (*@rest) unpacks the last condition value' }

# with / without / given
with @e -> @ ($h, *@t) { check ($h, @t.List), (1, (2, 3)), 'with -> @ ($h, *@t)' }
else                   { @fail.push('with -> @ (…) did not run') }
given @e -> ($a, $b, $c) { check $b, 2, 'given -> ($a,$b,$c)' }
without Nil -> $u { check $u, Nil, 'without -> $u still binds the (undefined) topic' }

# while: the condition value is the signature's single argument, per iteration
my @seen;
my $n = 2;
while ($n-- > 0 ?? [$n, "x$n"] !! Nil) -> ($num, $name) { @seen.push("$num/$name") }
check @seen.List, ('1/x1', '0/x0'), 'while -> ($num, $name) destructures each value';

my @seen2;
my $m = 1;
while ($m-- > 0 ?? [7, 8, 9] !! Nil) -> @ ($h, *@t) { @seen2.push($h, @t.elems) }
check @seen2.List, (7, 2), 'while -> @ ($h, *@t)';

# --- the plain binders these share a code path with, which must stay plain ---
if @e -> $x        { check $x.elems, 3, 'if -> $x still binds the whole value' }
if @e -> \elems    { check elems.elems, 3, 'if -> \elems (sigilless) still binds' }
if @e -> $x is copy { $x = 9; check $x, 9, 'if -> $x is copy is still writable' }
if 42 -> *@slurp   { check @slurp.List, (42,), 'if -> *@slurp still wraps in a list' }
unless 0 -> $x     { check $x, 0, 'unless -> $x still binds the condition' }
given 5 -> $y      { check $y, 5, 'given -> $y still binds the topic' }
with 5 -> $y       { check $y, 5, 'with -> $y still binds the topic' }
my $k = 1;
while ($k-- > 0 ?? 'v' !! Nil) -> $v { check $v, 'v', 'while -> $v still binds' }

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
