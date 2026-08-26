# Issue #34, third failure: TAP's `class Output::Handle does Output` is
#   has IO::Handle:D $.handle handles(:print<print>, :flush<flush>, :terminal<t>)
# — the RENAMING form of the `handles` trait, where the class exposes the KEY
# and calls the VALUE on the attribute (`.terminal` is IO::Handle's `.t`).
#
# Only `handles <a b>` / `handles "m"` / `handles *` parsed, so the paren form
# was consumed as nothing: the delegated names did not exist, and the role's
# stub `method print { ... }` was reached instead ("Stub code executed").
# A delegation is a method ON THE CLASS in Rakudo, so it also outranks anything
# a composed role brought in under that name — a stub and a default body alike.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

class Inner {
    method shout(Str $s) { "SHOUT:$s" }
    method t()           { 'inner-t' }
    method plain()       { 'inner-plain' }
}

# --- the renaming forms ------------------------------------------------------
class Renames {
    has Inner $.i handles(:say-it<shout>, :terminal<t>, 'plain') = Inner.new;
}
my $r = Renames.new;
check $r.say-it('hi'), 'SHOUT:hi', 'handles(:say-it<shout>) renames';
check $r.terminal,     'inner-t',  'handles(:terminal<t>) renames';
check $r.plain,        'inner-plain', 'a bare name in handles(…) delegates unrenamed';

class Arrow { has Inner $.i handles(loud => 'shout', <plain>) = Inner.new }
check Arrow.new.loud('yo'), 'SHOUT:yo', 'handles(name => "target") renames';
check Arrow.new.plain, 'inner-plain', 'an angle list inside handles(…) delegates';

# --- the spellings that already worked, which must stay working --------------
class Angle  { has Inner $.i handles <plain t> = Inner.new }
check Angle.new.plain, 'inner-plain', 'handles <a b> still delegates';
check Angle.new.t,     'inner-t',     'handles <a b> still delegates (second name)';
class Str1   { has Inner $.i handles 'plain' = Inner.new }
check Str1.new.plain,  'inner-plain', 'handles "m" still delegates';
class Star   { has Inner $.i handles * = Inner.new }
check Star.new.t,      'inner-t',     'handles * is still the catch-all';

# --- a delegation outranks a composed role's stub AND its default body -------
role Out {
    method print(Str $v) { ... }            # a requirement
    method terminal()    { 'ROLE-terminal' } # a default the class overrides
    method say(Str $v)   { self.print($v ~ '!') }
}
class Handle does Out {
    has Inner $.i handles(:print<shout>, :terminal<t>) = Inner.new;
}
my $h = Handle.new;
check $h.print('x'),    'SHOUT:x',  'a delegation satisfies a role STUB (not "Stub code executed")';
check $h.terminal,      'inner-t',  "a delegation outranks the role's own body";
check $h.say('y'),      'SHOUT:y!', 'the role calls back into the delegated method';

# a delegation is INHERITED, and a subclass's own method still overrides it
class Kid  is Handle { }
class Over is Handle { method terminal() { 'KID' } }
check Kid.new.terminal,  'inner-t', 'a delegation is inherited';
check Over.new.terminal, 'KID',     "…and a subclass's own method still overrides it";

# …and a class that implements the requirement itself is untouched by all this
class Own does Out {
    has Inner $.i = Inner.new;
    method print(Str $v) { 'OWN:' ~ $v }
}
check Own.new.print('z'),  'OWN:z',          'an own method satisfies the stub as before';
check Own.new.terminal,    'ROLE-terminal',  "…and the role's own body is still reached";

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
