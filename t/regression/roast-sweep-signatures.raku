# Signatures and their rendering, from the 2026-09-28 partial-file sweep
# (roast S06-signature/*, S06-other/introspection.t, S12-class/interface-
# consistency.t). Every line answers the same under Rakudo 2026.08.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku}] WANT [{$want.raku}]";
    }
}

# a literal parameter accepts only the same literal (outside-subroutine.t)
check(':("foo") ~~ :("bar")',  :("foo") ~~ :("bar"), False);
check(':("bar") ~~ :("bar")',  :("bar") ~~ :("bar"), True);
check(':(Str) ~~ :("foo")',    :(Str) ~~ :("foo"),   False);
check(':("foo") ~~ :(Str)',    :("foo") ~~ :(Str),   True);
check(':(1) ~~ :(1.0)',        :(1) ~~ :(1.0),       False);
check(':(1e0) ~~ :(1e0)',      :(1e0) ~~ :(1e0),     True);

# a multi with no proto has the signature Rakudo generates for it
multi sub m1(1, 2) { "m1" }
multi sub m1(1)    { "m2" }
check('proto-less multi signature', &m1.signature.raku, ':(;; Mu |)');
check('…and it accepts any capture', \(1, 2) ~~ &m1.signature, True);

# a Capture keeps a positional Pair a Pair (named-parameters.t)
check('Capture.raku with a positional Pair', \(1, 2, "a" => 3).raku, '\(1, 2, "a" => 3)');
check('…and a named one a colonpair', \(1, a => 3).raku, '\(1, :a(3))');

# a Signature inside a list renders as its literal (unspecified.t)
check('a Signature in a list round-trips', (:(Int $x),).raku, '(:(Int $x),)');

# ::?CLASS in a role reads as the generic it is (signature/introspection.t)
role RoleWithClassParam { sub a($a, ::?CLASS $c) { }; method sig { &a.signature.raku } }
check('::?CLASS in a role', RoleWithClassParam.sig, ':($a, ::?CLASS $c)');

# a default reads the parameter itself, not an outer name (code.t)
my $tracker;
sub foo(&foo = &foo) { $tracker = &foo }
try foo();
check('the inner &foo is undefined', $tracker.defined, False);

# a shape dimension may name an earlier parameter (shape.t)
sub dependent($n, @a[$n]) { }
check('shape from an earlier parameter binds', (try { dependent(3, Array.new(:shape(3))); True }) // False, True);
check('…and refuses a mismatch', (try { dependent(4, Array.new(:shape(3))); True }) // False, False);
check('an enum is a dimension', (my @e[Bool]).shape, (2,));

# a method's implicit *%_ shows, except in an `is hidden` class
class SigFoo { method m1($a) { 1 } }
class SigBar is SigFoo is hidden { method m3($a) { 3 } }
check('*%_ in a method signature', SigFoo.^lookup('m1').signature.raku.contains('*%_'), True);
check('…but not in a hidden class', SigBar.^lookup('m3').signature.raku.contains('*%_'), False);
check('…whose methods refuse unknown nameds', (try { SigBar.new.m3(1, :x); True }) // False, False);

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
