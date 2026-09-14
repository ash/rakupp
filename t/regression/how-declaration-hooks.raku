# Regression: a module-supplied metaclass DRIVES the declaration. Rakudo hands a
# class to its HOW one piece at a time — new_type, add_attribute per attribute,
# add_method per method, compose — and a metaclass does its work in whichever of
# those it overrides. rakupp built the class with its own metamodel and called
# `compose` alone, so a metaclass that works anywhere else was silently ignored:
# OO::Monitors adds its lock in `new_type` and wraps every method in lock/unlock
# in `add_method`, and a `monitor` declared through it was a PLAIN CLASS whose
# own test suite then lost counts to the race it exists to prevent (issue #86).
#
# The other half is construction: a POPULATE the metaclass ADDS has to run, or
# nothing it sets up per instance is ever there.
#
# Every expectation below was checked against Rakudo.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppHowHooks;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

hooked H { has $.n; method own() { 'OWN' }; method peek() { $!n } }

# --- every hook was called, new_type first --------------------------------
my @order = H.HOW.hook-order(H).split(',');
ck(@order[0], 'new_type',                    'new_type is the first hook');
ck(?@order.first(* eq 'add_method:own'), True, 'add_method saw the declared method');
ck(?@order.first(* eq 'compose'), True,      'compose ran');
ck(?@order.first(* eq 'add_method:POPULATE'), True, '…and the method it added there');

# --- what each hook DID is on the class -----------------------------------
ck(H.^attributes.map(*.name).sort.join(','), '$!SECRET,$!n',
                                             'the attribute new_type added is on the class');
ck(H.own, 'W:OWN',                           'the wrap add_method applied is in force');
ck(H.new(n => 5).own, 'W:OWN',               '…on an instance too');
ck(H.new(n => 5).peek, 'W:5',                'a wrapped method reads the attribute back');
# (NOT asserted: the generated ACCESSOR `.n`. Rakudo makes accessors in compose
# and adds them through add_method, so `.n` is wrapped there; rakupp answers a
# public attribute natively rather than from a method table, so it is not. The
# names a class DECLARES — which is what a monitor's callers use — agree.)

# --- the POPULATE it added RUNS, once, per instance -----------------------
ck(H.HOW.secret-of(H, H.new), 4242,          'the added POPULATE ran at construction');
ck(H.HOW.secret-of(H, H.new(n => 7)), 4242,  '…however the object was constructed');

# --- an ORDINARY class is untouched by any of it --------------------------
class Plain { has $.v = 3; method m() { 'plain' } }
ck(Plain.new.m, 'plain',                     'a plain class keeps its methods unwrapped');
ck(Plain.new.v, 3,                           '…and its attributes');
ck(Plain.^attributes.map(*.name).join(','), '$!v', '…and gains no attribute');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
