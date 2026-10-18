# Construction semantics a natively compiled class (`rakupp --exe`, the fully
# native path) used to get wrong, written so the program stays native: no
# roles, no CATCH, so t/exe/run.raku compares the binary with the interpreter.
#
# - `is required` was never checked: the class registration the code
#   generator emits did not carry it, so `N.new` constructed with the
#   attribute unset, with or without a BUILD.
# - An attributive parameter (`submethod BUILD(:$!t)`, `method set($!x)`,
#   `*%!opts`) bound a local looked up under the named key "!t" and never
#   wrote the attribute: a private attribute stayed Any, a parameter default
#   was lost. (A public one only looked right because the default
#   constructor had filled it from the argument already.)
# - `$!t = Nil` stored the Nil, where Nil resets the attribute to its
#   default: Any, or a typed attribute's type object.
#
# Every expectation was checked against Rakudo v2026.09 (2026-10-18).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub outcome(&code) {
    my $r = try code();
    $! ?? $!.^name !! 'lived'
}

# is required
class R1 { has $.t is required }
check outcome({ R1.new }), 'X::Attribute::Required', 'required, no BUILD, nothing passed';
check R1.new(:t(1)).t, 1, '…and passed';
class R2 { has $.t is required("it names the thing") }
check (try R2.new) // $!.message, "The attribute '\$!t' is required because it names the thing,\nbut you did not provide a value for it.", 'the reason is in the message';
class R3 { has $.t is required; submethod BUILD() { } }
check outcome({ R3.new(:t(1)) }), 'X::Attribute::Required', 'a BUILD that ignores the argument';
class R4 { has $.t is required; submethod BUILD(:$t) { $!t = $t if $t } }
check outcome({ R4.new }), 'X::Attribute::Required', 'an assignment that does not run';
class R5 { has $.t is required; submethod BUILD(:$t) { $!t = $t } }
check outcome({ R5.new }), 'lived', 'an assignment of an undefined value initializes it';
class R6 { has $.t is required; submethod BUILD(:$!t) { } }
check outcome({ R6.new }), 'lived', 'BUILD(:$!t) initializes it with nothing passed';
check R6.new(:t(5)).t, 5, '…and binds what is passed';
class R7 { has $.t is required; submethod TWEAK() { $!t = 1 } }
check outcome({ R7.new }), 'X::Attribute::Required', 'a TWEAK is too late';
class P8 { has $.t is required; submethod BUILD(:$!t) { } }
class R8 is P8 { }
check outcome({ R8.new }), 'lived', 'a parent BUILD, from a subclass';

# attributive parameters write the attribute
class A1 { has $!p; submethod BUILD(:$!p) { }; method p { $!p } }
check A1.new(:p(9)).p, 9, 'a private attribute from BUILD(:$!p)';
class A2 { has $.t; submethod BUILD(:$!t = 3) { } }
check A2.new.t, 3, 'a parameter default';
check A2.new(:t(4)).t, 4, '…not used when passed';
class A3 { has @.a; has %.h; submethod BUILD(:@!a, :%!h) { } }
check A3.new(:a(1, 2), :h(x => 1)).a, [1, 2], 'an array attribute';
check A3.new(:a(1, 2), :h(x => 1)).h, {x => 1}, 'a hash attribute';
class A4 { has $!x; method set($!x) { self }; method x { $!x } }
check A4.new.set(7).x, 7, 'a positional attributive parameter';
class A5 { has %!opts; submethod BUILD(*%!opts) { }; method opts { %!opts } }
check A5.new(:a(1), :b(2)).opts, {a => 1, b => 2}, 'an attributive slurpy';
class A6 { has $!a; has $!b; submethod BUILD(:$!a, :$!b = $!a * 2) { }; method b { $!b } }
check A6.new(:a(5)).b, 10, 'a later default sees the earlier attribute';

# Nil resets
class N1 { has $.t; submethod BUILD() { $!t = Nil } }
check N1.new.t, Any, '$!t = Nil is Any';
class N2 { has Int $.n = 3; method clear { $!n = Nil; self } }
check N2.new.clear.n, Int, 'a typed attribute goes back to its type';
class N3 { has $.t; submethod BUILD(:$!t) { } }
check N3.new(:t(Nil)).t, Any, 'Nil passed to BUILD(:$!t)';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
