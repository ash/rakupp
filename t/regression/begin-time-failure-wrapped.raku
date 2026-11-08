# What dies in a BEGIN, a CHECK or a constant's initializer is
# X::Comp::BeginTime, worded as Rakudo's: "An exception X::AdHoc+{X::Comp}
# occurred while evaluating a BEGIN:\nb". The inner exception of a BEGIN and
# of a constant (which Rakudo evaluates as a BEGIN) does X::Comp, a CHECK's
# not. An EVAL's statement-form BEGIN, an EVAL's CHECK and a constant whose
# initializer dies were not wrapped at all, and the wording was "An exception
# occurred while evaluating a BEGIN: b". What the store into a `%` constant
# refuses stays as it is.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub caught($code) { try EVAL $code; ($!.^name, $!.message, ($!.?exception // Nil).^name) }

ck caught(q[BEGIN die "b"]),
   ('X::Comp::BeginTime', "An exception X::AdHoc+\{X::Comp} occurred while evaluating a BEGIN:\nb", 'X::AdHoc+{X::Comp}'),
   'a statement-form BEGIN';
ck caught(q[BEGIN { die "b" }]),
   ('X::Comp::BeginTime', "An exception X::AdHoc+\{X::Comp} occurred while evaluating a BEGIN:\nb", 'X::AdHoc+{X::Comp}'),
   'a BEGIN block';
ck caught(q[CHECK die "k"]), ('X::Comp::BeginTime', "An exception X::AdHoc occurred while evaluating a CHECK:\nk", 'X::AdHoc'),
   'a CHECK';
ck caught(q[CHECK { die "k" }]), ('X::Comp::BeginTime', "An exception X::AdHoc occurred while evaluating a CHECK:\nk", 'X::AdHoc'),
   'a CHECK block';
ck caught(q[constant C = die "x"]),
   ('X::Comp::BeginTime', "An exception X::AdHoc+\{X::Comp} occurred while evaluating a BEGIN:\nx", 'X::AdHoc+{X::Comp}'),
   "a constant's initializer";
ck caught(q[constant C2 = X::NYI.new(feature => "f").throw]),
   ('X::Comp::BeginTime', "An exception X::NYI+\{X::Comp} occurred while evaluating a BEGIN:\nf not yet implemented. Sorry.", 'X::NYI+{X::Comp}'),
   'another exception';
ck caught(q[constant %h = 42])[0], 'X::Hash::Store::OddNumber', "what a `%` constant's store refuses";
ck do { try EVAL q[BEGIN { die "oh noes!" }]; $!.exception.message }, 'oh noes!', "the inner message";
ck EVAL(q[BEGIN my $x = 5; $x]), 5, 'a statement-form BEGIN declares, as before';
ck EVAL(q[CHECK { 1 }; 2]), 2, 'a CHECK that lives';

say 'PASS';
