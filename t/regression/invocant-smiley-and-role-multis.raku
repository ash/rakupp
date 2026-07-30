# From drilling JSON::Class through its load chain — the module now LOADS in
# full. Eight fixes; the ones observable in-process are pinned here:
#
#   * a `unit role` file's `use` statements execute BEFORE parent/role
#     resolution, so `also does X` across files finds X composed (the whole
#     JSON::Class Attr chain: Descriptor <- Attr <- Associative).
#   * `is built(:bind)` on a PRIVATE attr: .new sets it by name.
#   * traits on sigilless params: `\key is raw`.
#   * `(::?CLASS:U:)` / `(::?CLASS:D:)`: the invocant marker used to parse its
#     :D/:U smiley into a param it then THREW AWAY, so every candidate looked
#     identical; and `(::?CLASS:U)` with NO colon is an anonymous POSITIONAL
#     (verified against Rakudo's own signature rendering).
#   * a bare `{*}` proto METHOD is the group definition, not a candidate — it
#     used to enter dispatch with its (|) slurpy and win.
#   * invocant definedness drives dispatch: :U for the type object, :D for
#     instances, constrained beats unconstrained.
#   * one role's own multi candidates never conflict with themselves.
#   * `will complain { … }` on a subset parses (message-only; parse-only).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# invocant smileys split type-object and instance behaviour
class D {
    proto method g(|) {*}
    multi method g(::?CLASS:U:) { 'undef' }
    multi method g(::?CLASS:D:) { 'def' }
}
check(D.g,     'undef', ':U catches the type object');
check(D.new.g, 'def',   ':D catches the instance');

# the same from a role, via also does — and no self-conflict
role R {
    proto method h(|) {*}
    multi method h(::?CLASS:U:) { 'r-undef' }
    multi method h(::?CLASS:D:) { 'r-def' }
}
class C { also does R }
check(C.h,     'r-undef', 'role multis with invocant smileys compose');
check(C.new.h, 'r-def',   'and dispatch by definedness');

# is built on a private attr
role Desc {
    has Mu $!declarant is built(:bind);
    method declarant { $!declarant }
}
class Holder { also does Desc }
check(Holder.new(declarant => Int).declarant.^name, 'Int', 'is built lets .new set a private attr');

# sigilless param traits
sub take-raw(\key is raw) { key }
check(take-raw(42), 42, 'a trait on a sigilless param parses');

# will complain parses and the subset still enforces
my subset SmallInt of Int will complain { "too big" } where * < 10;
check(5 ~~ SmallInt, True,  'a will-complain subset accepts');
check(50 ~~ SmallInt, False, 'and rejects');

# the CROSS-FILE shape: unit-role files whose `use` sits inside the body; the
# body uses must run before parent resolution or the composing class never sees
# the transitive private attr ("Attribute $!declarant not declared")
use lib $?FILE.IO.parent.add('lib').Str;
use JX::Assoc;
check(JX::Assoc.new(declarant => Int).who, 'Int', 'a unit-role chain across files composes transitively');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
