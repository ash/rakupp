# Regression: a private method `method !name` and a public `method name` of the
# SAME name are DISTINCT methods and must coexist — `self.name` calls the public
# one, `self!name` the private one. rakupp stored both under the bare name, so the
# private shadowed the public; a public method that (via a role) called `self.name`
# while the class also had a private `!name` recursed forever.
# (Surfaced by zef: Zef::Repository has a private `!plugins`, while the Pluggable
# role gives it a public `.plugins`; `self.plugins` was hitting `!plugins`.)
# Contract: exit 0 + last line PASS.
my @fail;

class C {
    method plugins { 'public' }
    method !plugins { 'private' }
    method via-dot  { self.plugins }
    method via-bang { self!plugins }
}
@fail.push('dot')  unless C.new.via-dot  eq 'public';
@fail.push('bang') unless C.new.via-bang eq 'private';

# a private-only method is not reachable via `.` (it throws)
class P { method !secret { 1 } }
my $threw = False;
try { P.new.secret; CATCH { default { $threw = True } } }
@fail.push('no-dot-private') unless $threw;

# the exact zef shape: a role gives a public method, the class adds a private one
# of the same name that calls the PUBLIC one — must not recurse.
role Pluggable { method run { 'from-role' } }
class D does Pluggable {
    method !run { 'own-private' }
    method go   { self.run }        # the role's public .run, NOT self's private !run
    method priv { self!run }
}
@fail.push('role-public')  unless D.new.go   eq 'from-role';
@fail.push('own-private')  unless D.new.priv eq 'own-private';

# private methods still work the ordinary way (no same-name public)
class E { method !h { 7 }; method use-it { self!h * 2 } }
@fail.push('plain-private') unless E.new.use-it == 14;

# a private STUB in one role is satisfied by a private impl in another (RT #125606):
# the requirement is keyed `!foo` and the private impl `!foo` fulfils it.
role WithPrivate     { method !foo { 'p' } }
role WithPrivateStub { method !foo { ... } }
class ClassPrivate does WithPrivate does WithPrivateStub { method callit { self!foo } }
@fail.push('private-stub-resolution') unless ClassPrivate.new.callit eq 'p';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
