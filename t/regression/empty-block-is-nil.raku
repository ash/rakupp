# Regression: a block with no value evaluated to an undefined Any.
#
# In Rakudo every value-less block is Nil, uniformly — an empty sub or method,
# `try {}`, a taken-but-empty `if`, an empty loop body collected by `do for`.
# rakupp seeded execBlock's running value with Any instead, so all of them came
# back Any.
#
# It stayed invisible for a long time because assigning Nil to a scalar resets
# it to the container's default, which for an untyped `my` IS Any: only .raku,
# .WHAT and a list that holds the value unassigned can tell the two apart.
#
# The neighbouring case must NOT move: `my $x;` is a DECLARATION whose value is
# a genuinely undefined Any, and it stays Any in both engines.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub s-empty  { }
sub s-semi   { ; }
sub s-if     { if 1 { } }
sub s-try    { try { } }
sub s-given  { given 5 { } }
sub s-decl   { my $x; }
sub s-value  { my $x = 5; }
class C { method m { } }
my $anon   = sub { };
my $pointy = -> { };

# .raku is taken at the CALL site: binding a list to a scalar parameter would
# itemise it, and `$(Nil, Nil)` is not what the engine rendered.
sub is-raku($desc, Str $got, Str $want) {
    @fail.push("$desc -> $got, want $want") unless $got eq $want;
}

is-raku('empty sub',       (s-empty()).raku,   'Nil');
is-raku('bare semicolon',  (s-semi()).raku,    'Nil');
is-raku('empty if taken',  (s-if()).raku,      'Nil');
is-raku('empty try',       (s-try()).raku,     'Nil');
is-raku('empty given',     (s-given()).raku,   'Nil');
is-raku('empty method',    (C.m).raku,         'Nil');
is-raku('empty anon sub',  ($anon()).raku,     'Nil');
is-raku('empty pointy',    ($pointy()).raku,   'Nil');

# the declaration next door keeps its Any
is-raku('bare my',         (s-decl()).raku,    'Any');
is-raku('my with a value', (s-value()).raku,   '5');

# collected in a list the values stay Nil, one per iteration
is-raku('do for empty body', ((do for 1..2 { })).raku, '(Nil, Nil)');
is-raku('map empty block',   ((1, 2).map(-> $x { })).raku, '(Nil, Nil).Seq');

# Assigning one to a scalar still lands on Any — that is what Nil MEANS in an
# assignment, and it is why the divergence hid for so long. Not asserted here:
# the --target=js backend holds scalars in plain JS bindings with no container to
# reset, so it keeps the Nil, and this file is part of that backend's corpus gate.
@fail.push('defined') if s-empty().defined;

if @fail {
    note "FAIL: $_" for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
