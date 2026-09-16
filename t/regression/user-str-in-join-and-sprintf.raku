# Regression: an object's own `method Str` in the paths that stringify from
# OUTSIDE the interpreter.
#
# `@list.join(';')` asked the object; `join(';', @list)` — the SUB — did not,
# and neither did `sprintf("%s", $obj)`. Both reached a Value's raw rendering
# and printed Class<address>. Dice::Roller's entire display is built that way
# (`join('; ', @!rolls)` over objects whose Str renders the dice), so a parsed
# "1d20" stringified as Dice::Roller::Expression<140234…>.
#
# Still open, deliberately not asserted here: an object used as a HASH KEY
# (`%( $obj => 1 )`) is keyed by its raw rendering rather than its Str.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

class C { has $.n; method Str(--> Str:D) { "C$.n" } }
my $c = C.new(n => 1);
my @l = C.new(n => 1), C.new(n => 2);

check join(';', $c),      'C1',    'the join SUB asks a user Str';
check join(';', @l),      'C1;C2', 'for every element';
check @l.join(';'),       'C1;C2', 'as the method always did';
check sprintf('%s', $c),  'C1',    '%s is a .Str';
check sprintf('%s-%s', |@l), 'C1-C2', 'for each argument it consumes';
check '%s'.sprintf($c),   'C1',    'in the method spelling too';
check ~@l,                'C1 C2', 'and ~ on a list, which already worked';
check $c.Str,             'C1',    'as does .Str itself';

# The join sub keeps its own rules: a separator that is not a Str, nested
# lists flattened, an empty call.
check join('-', 1, 2, 3),        '1-2-3',  'plain values are unaffected';
check join('-', [1, 2], [3]),    '1-2-3',  'and nested lists still flatten';
check join(0, 'a', 'b'),         'a0b',    'a non-Str separator stringifies';
check join(''),                  '',       'a separator alone joins nothing';

# %s on things that are not objects must render exactly as before.
check sprintf('%s', 42),         '42',     'an Int';
check sprintf('%s', 1.5),        '1.5',    'a Rat';
check sprintf('%s', 'x'),        'x',      'a Str';
check sprintf('%-5s|', 'ab'),    'ab   |', 'width and left-justify still apply';
check sprintf('%.2s', 'abcd'),   'ab',     'and precision';
check sprintf('%s%s', 'a', 'b'), 'ab',     'and two of them in one format';

# A class WITHOUT a Str is untouched — it renders as it always did.
class D { has $.n }
check (join(';', D.new(n => 1)) ~~ /^ 'D' /).so, True, 'a class with no Str keeps its rendering';

# A Str inherited from a parent counts too.
class E is C { }
check join(';', E.new(n => 9)), 'C9', 'an inherited Str is found';

# …and one a role supplies.
role S { method Str(--> Str:D) { 'from-role' } }
class F does S { }
check sprintf('%s', F.new), 'from-role', 'so is one from a role';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
