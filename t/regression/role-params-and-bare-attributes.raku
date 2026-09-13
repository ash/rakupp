# Regression: the "Variable 'X' is not declared" cluster — 22 distributions in
# the ecosystem sweep died on a variable that IS declared, in three distinct
# ways. None of them is a check bug: all three are places where a name really
# was out of scope at run time.
#
#   * a ROLE PARAMETER is in scope for the role's attribute DEFAULTS, not only
#     for its method bodies — `role Instruction[$ins] { has $.instruction = $ins }`
#     is how Docker::File names a dozen instruction classes
#   * a TWIGIL-LESS attribute (`has @items`, read as a bare `@items`) resolved
#     only on the WRITE path. Assigning to one worked and reading it died,
#     which is how Dependency::Sort and Log::D failed.
#   * a role binds its own parameter DEFAULTS, so PUNNING it — using the role
#     directly, as Acme::Cow does with `Acme::Cow.new(...)` — sees them. Reached
#     through `does` the same methods already worked.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- a role parameter reaches an attribute default ----------------------
# Two compositions of the same role, so a default that ignored its parameter
# and answered one fixed value could not pass both rows.
role Instruction[$ins] {
    has $.instruction = $ins;
}
class Maintainer does Instruction['MAINTAINER'] { }
class From       does Instruction['FROM']       { }
ck Maintainer.new.instruction, 'MAINTAINER', 'an attribute default reads its role parameter';
ck From.new.instruction, 'FROM', '…and a second composition gets its own value';

# ---- a twigil-less attribute is READ as well as written -----------------
class Bare {
    has @items;
    has $count;
    has %seen;
    method add($x) { @items.push($x); $count = @items.elems; %seen{$x} = True; self }
    method n()     { @items.elems }
    method last()  { @items.end }
    method saw($x) { %seen{$x}:exists }
    method c()     { $count }
}
my $b = Bare.new;
ck $b.n, 0, 'reading an untouched bare attribute gives the empty value';
ck $b.add('a').add('b').n, 2, 'a bare attribute can be written and read back';
ck $b.last, 1, '…and read by another method';
ck $b.c, 2, '…including a bare scalar written from a method';
ck $b.saw('a'), True, '…and a bare hash';
ck $b.saw('z'), False, '…which answers honestly for a key it never saw';

# an inherited bare attribute is reached the same way
class Derived is Bare { method twice() { self.n * 2 } }
ck Derived.new.add('x').twice, 2, 'a bare attribute is readable from a subclass method';

# ---- a role binds its own parameter defaults, so a pun sees them --------
my constant DEFAULT-COW = 'moo';
role Cow[$cow = DEFAULT-COW] {
    has Str $.cow is rw;
    multi method new(*%_) { self.bless(:$cow, |%_) }
}
ck Cow.new.cow, 'moo', 'a PUNNED role sees its own parameter default';
class Sheep does Cow['baa'] { }
ck Sheep.new.cow, 'baa', '…and a composing class still wins with its own argument';
class Quiet does Cow { }
ck Quiet.new.cow, 'moo', '…while a bare `does` takes the default';

# ---- and the check still CATCHES a genuinely undeclared name ------------
# The three fixes above widen what counts as declared, so the negative case
# matters as much: a name nothing declares must still die.
my $caught = 'did NOT die';
try { EVAL 'class NoSuch { method m() { @never-declared-anywhere.elems } }; NoSuch.new.m';
      CATCH { default { $caught = 'died' } } }
ck $caught, 'died', 'a name no attribute and no `my` declares still dies';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
