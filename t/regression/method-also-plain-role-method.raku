#?requires Method::Also
# Regression: a role whose method carries `is also<alias>` (Method::Also) died
# while being composed — AliasableRoleHOW's `specialize_with` hook assigns
# `$*TYPE-ENV`, a dynamic variable Rakudo's compiler provides around
# specialization and Raku++ does not (it carries the aliases itself, through
# ClassInfo::alsoRoleAliases). The hook's failure turned `use PDF::Lite` into
# "Dynamic variable $*TYPE-ENV not found" at PDF::COS::Tie's dict classes.
# Rakudo's answer for these shapes is "no alias from the MOP"; that is what
# Raku++ produces on its own path, so the composition must not die.
# Contract: exit 0 + last line PASS.
use Method::Also;
my @fail;

# a PLAIN role method keeps its real name and does NOT get the alias (Method::Also's
# is_dispatcher guard: only a proto/multi group is aliased into the composing class)
role Plain { method solo is also<lonely> { 'x' } }
class P does Plain { }
@fail.push("plain composes") unless (try { P.new.solo } // '') eq 'x';
@fail.push("plain gets no alias") if (try { P.new.can('lonely') } // False);

# …while a proto's alias still reaches the composing class and dispatches
role Ber {
    proto method bar_ber (|) is also<bar-ber> { * }
    multi method bar_ber (Str) { 'Str' }
}
class B does Ber { }
@fail.push("proto alias composes") unless (try { B.new.bar-ber('a') } // '') eq 'Str';

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
