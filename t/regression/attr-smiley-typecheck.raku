# Regression: an attribute type smiley (`has Int:D $.a` / `has Int:U $.a`) is
# enforced on the slot's FINAL value at construction, matching Rakudo's
# X::TypeCheck::Attribute::Default. A `:D` attr wants a defined value, a `:U`
# attr wants an undefined one; the check fires for a bad default OR a bad
# construction arg, and stays quiet when the attr got no value at all.
# (Surfaced by S12-attributes/smiley.t once `class {…}.new` parsed correctly.)
# Contract: exit 0 + last line PASS.
my @fail;

sub throws(&code) { my $t = False; { code(); CATCH { default { $t = True } } }; $t }

# :D with a defined default is fine; :U with an undefined default is fine
@fail.push('D-ok')  unless (class { has Int:D $.a = 42 }).new.a == 42;
@fail.push('U-ok')  unless (class { has Int:U $.a = Int }).new.a.^name eq 'Int';

# :D wants defined — an undefined default must throw
@fail.push('D-bad-default') unless throws { (class { has Int:D $.a = Int }).new };
# :U wants undefined — a defined default must throw
@fail.push('U-bad-default') unless throws { (class { has Int:U $.a = 42 }).new };

# a bad construction ARG is caught too
@fail.push('D-bad-arg') unless throws { (class { has Int:D $.a }).new(a => Int) };
# a good arg satisfies a defaultless :D
@fail.push('D-good-arg') unless (class { has Int:D $.a }).new(a => 7).a == 7;

# no smiley = no constraint (unchanged behaviour)
@fail.push('plain') unless (class { has Int $.a = 5 }).new.a == 5;
# defaultless :U with no arg keeps its undefined type object — no throw
@fail.push('U-empty') unless (class { has Int:U $.a }).new.a.^name eq 'Int';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
