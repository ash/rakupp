# Issue #110: the default constructor stored any value in a typed attribute.
# An assignment to the same attribute after construction was checked, and so
# was a typed variable, but `.new` asked only seven core types (Int UInt Num
# Rat Str Bool Complex) and the subsets — and only of a DEFINED value. So
# `has Str $.s` took `Any`, `has A $.a` took any object at all, `has int $.i`
# took a Str, and `has Date() $.d` kept the Str it was given. A program tested
# under rakupp then failed under Rakudo on its first `.new`.
#
# A value given by name is ASSIGNED to its attribute — by `new`, `bless` and
# `.clone` alike — so it passes what `$!name = value` would. A Nil is a reset
# to the attribute's default (its `is default`, when it has one), not a store.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.08 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
# 'ok', or the type of what was thrown
sub outcome(&c) {
    my $r = 'ok';
    try { c(); CATCH { default { $r = .^name } } }
    $r
}
# refused, whatever the exception (the natives' differs between engines)
sub refused(&c) { outcome(&c) ne 'ok' }
constant BAD = 'X::TypeCheck::Assignment';

my class A {}
my class A2 is A {}
my class B {}
my role R {}
my subset Small of Int where * < 10;
my enum Color <red green>;

# -- the issue's own table ---------------------------------------------------
{
    my class C { has Str $.s is rw; has Int $.n }
    my $any;
    check(outcome({ C.new(s => $any) }),               BAD, 'A: C.new(s => Any) into a Str');
    check(outcome({ C.new(s => 42) }),                 BAD, 'B: C.new(s => 42)');
    check(outcome({ my $c = C.new; $c.s = $any }),     BAD, 'C: $c.s = Any after .new');
    check(outcome({ C.new(n => 'x') }),                BAD, 'D: C.new(n => "x") into an Int');
    check(outcome({ my Str $x = $any }),               BAD, 'E: my Str $x = Any');
    my $e;
    try { C.new(s => $any); CATCH { default { $e = $_ } } }
    check(($e ?? $e.symbol !! Nil), '$!s', 'the failure names the attribute');
}

# -- type objects are values too ---------------------------------------------
{
    my class C { has Str $.s; has Int $.n; has Numeric $.num }
    check(outcome({ C.new(s => Any) }),   BAD,  'Any into Str');
    check(outcome({ C.new(n => Cool) }),  BAD,  'Cool into Int');
    check(outcome({ C.new(s => Mu) }),    BAD,  'Mu into Str');
    check(outcome({ C.new(num => Any) }), BAD,  'Any into Numeric');
    check(outcome({ C.new(s => Str) }),   'ok', 'Str into Str');
    check(outcome({ C.new(n => Int) }),   'ok', 'Int into Int');
    check(outcome({ C.new(num => Int) }), 'ok', 'Int into Numeric');
}

# -- every declared type, not only the core seven ----------------------------
{
    my class C {
        has A $.a; has R $.r; has Numeric $.num; has Cool $.cool; has Real $.real;
        has Color $.color; has Callable $.cb; has Date $.date; has Hash $.hsh; has Mu $.mu;
    }
    check(outcome({ C.new(a => B.new) }),      BAD,  'a B into an A');
    check(outcome({ C.new(a => B) }),          BAD,  'the B type object into an A');
    check(outcome({ C.new(a => A2.new) }),     'ok', 'a subclass into an A');
    check(outcome({ C.new(a => A2) }),         'ok', "…and the subclass's type object");
    check(outcome({ C.new(r => A.new) }),      BAD,  'an A into a role R');
    check(outcome({ C.new(num => 'x') }),      BAD,  'a Str into Numeric');
    check(outcome({ C.new(cool => A.new) }),   BAD,  'an A into Cool');
    check(outcome({ C.new(real => 1i) }),      BAD,  'a Complex into Real');
    check(outcome({ C.new(color => 1) }),      BAD,  'an Int into an enum');
    check(outcome({ C.new(color => green) }),  'ok', 'a member into its enum');
    check(outcome({ C.new(cb => 5) }),         BAD,  'an Int into Callable');
    check(outcome({ C.new(cb => -> { 1 }) }),  'ok', 'a block into Callable');
    check(outcome({ C.new(date => 'x') }),     BAD,  'a Str into Date');
    check(outcome({ C.new(hsh => 5) }),        BAD,  'an Int into Hash');
    check(outcome({ C.new(mu => Mu) }),        'ok', 'Mu into Mu');
}

# -- a subset, and a Nil reset (URI's `has Port $.port` takes `port => Nil`) --
{
    my class C { has Small $.small }
    check(outcome({ C.new(small => 20) }),  BAD,  'a value outside the subset');
    check(outcome({ C.new(small => 5) }),   'ok', 'a value inside it');
    check(outcome({ C.new(small => Nil) }), 'ok', 'Nil resets rather than stores');
}

# -- a definiteness smiley holds for what `new` is given ----------------------
{
    my class C { has Int:D $.d = 1; has Int:U $.u }
    check(outcome({ C.new(d => Int) }), BAD,  'a type object into Int:D');
    check(outcome({ C.new(d => Nil) }), BAD,  'Nil resets Int:D to an undefined default');
    check(outcome({ C.new(d => 5) }),   'ok', 'a defined Int into Int:D');
    check(outcome({ C.new(u => 5) }),   BAD,  'a defined Int into Int:U');
    check(outcome({ C.new(u => Int) }), 'ok', 'a type object into Int:U');
}

# -- natives take only what unboxes to them ----------------------------------
{
    my class C { has int $.i; has num $.n; has str $.t; has uint8 $.b }
    check(refused({ C.new(i => '7') }),   True,  'a Str into int');
    check(refused({ C.new(i => 4.2e0) }), True,  'a Num into int');
    check(refused({ C.new(i => Int) }),   True,  'a type object into int');
    check(refused({ C.new(n => 1) }),     True,  'an Int into num');
    check(refused({ C.new(t => 42) }),    True,  'an Int into str');
    check(refused({ C.new(b => 'x') }),   True,  'a Str into uint8');
    check(C.new(i => 7).i,     7,     'an Int into int');
    check(C.new(n => 1e0).n,   1e0,   'a Num into num');
    check(C.new(t => 'x').t,   'x',   'a Str into str');
}

# -- a coercion type converts what it is given, whatever the type ------------
{
    my class C { has Date() $.d; has Str() $.s; has Int() $.i }
    check(C.new(d => '2020-01-02').d, Date.new(2020, 1, 2), 'Date() converts a Str');
    check(C.new(s => 42).s,           '42',                 'Str() converts an Int');
    check(C.new(i => '42').i,         42,                   'Int() converts a Str');
}

# -- a Nil resets to the attribute's `is default` -----------------------------
{
    my class C { has $.x is default(5) = 7; has Str $.s is default('dflt') }
    check(C.new(x => Nil).x, 5,      'Nil takes `is default`, not the initializer');
    check(C.new(s => Nil).s, 'dflt', '…on a typed attribute too');
    check(C.new.x,           7,      'with no argument the initializer runs');
}

# -- bless and clone assign as `new` does -------------------------------------
{
    my class C { has Str $.s }
    my class Sh { has Str $.s; method new(*%h) { self.bless(|%h) } }
    check(outcome({ C.bless(s => Any) }),         BAD,  'bless checks too');
    check(outcome({ Sh.new(s => 42) }),           BAD,  '…through a custom new');
    check(outcome({ C.new.clone(s => 42) }),      BAD,  'clone checks its twiddles');
    check(C.new(s => 'a').clone(s => 'b').s, 'b', '…and takes a good one');
}

# -- what is not the default constructor's to check --------------------------
{
    # a class with its own BUILD binds its attributes itself
    my class Bu { has Str $.s; submethod BUILD(:$s) { $!s = 'built' } }
    check(Bu.new(s => 42).s, 'built', 'a BUILD that ignores the argument');
    # a role's type capture is checked as what it was bound to
    my role PR[::T] { has T $.v }
    my class Q does PR[Int] {}
    check(outcome({ Q.new(v => 'x') }), BAD,  'a Str into PR[Int]');
    check(Q.new(v => 5).v,              5,    'an Int into PR[Int]');
    # a Proxy stands in for the value
    my class P { has Int $.x }
    check(P.new(x => Proxy.new(FETCH => { 5 }, STORE => -> $, $ { })).x, 5, 'a Proxy');
}

if @fail { note "FAILED: $_" for @fail; say 'FAIL' } else { say 'PASS' }
