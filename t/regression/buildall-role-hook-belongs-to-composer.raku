# Regression: a composed ROLE is not an ancestor, so BUILDALL must not give it a
# turn of its own.
#
# The MRO walk that made a parent class's BUILD run again (issue #72, `fez
# login`) collected composed roles along with the real parents — the first
# `does` even arrives AS the parent — and ran each one's BUILD/TWEAK on a turn
# of its own. But composition FLATTENS a role's methods into the composing
# class, where the class's own declaration wins and the role's copy is
# discarded. So a class that declared its own TWEAK ran the role's as well,
# in addition to the one that had overridden it.
#
# Sparrow6 is the shape that exposes it (issue #75): its Range check-context
# declares a TWEAK that splits stdout into `foo`..`bar` streams and composes a
# role whose TWEAK builds the flat unsplit context. With both running, every
# check saw one extra stream holding the whole document and ten of its 160 CI
# tests failed.
#
# The other half of the pair is what the walk must STILL do: a real parent
# class's own BUILD and TWEAK keep their turns, interleaved per class. Both
# directions are here on purpose — a change that repairs either one by breaking
# the other is the failure mode.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eq $want { say "ok - $desc" }
    else { $fails++; note "FAIL: $desc — got '$got', want '$want'" }
}

# A role's hook, with no class declaration to beat it, still runs.
role R1 { has @.l is rw; method TWEAK { @!l.push: 'r1' } }
class C1 does R1 { }
ck C1.new.l.join('|'), 'r1', 'role TWEAK runs when the class declares none';

# …and loses to the class's own, instead of running beside it.
role R2 { has @.l is rw; method TWEAK { @!l.push: 'r2' } }
class C2 does R2 { method TWEAK { @!l.push: 'c2' } }
ck C2.new.l.join('|'), 'c2', 'a class TWEAK replaces the role TWEAK it overrides';

# The same for BUILD, which a role spells as a submethod.
role R3 { has @.l is rw; submethod BUILD { @!l.push: 'r3' } }
class C3 does R3 { }
ck C3.new.l.join('|'), 'r3', 'role BUILD runs when the class declares none';

role R4 { has @.l is rw; submethod BUILD { @!l.push: 'r4' } }
class C4 does R4 { submethod BUILD { @!l.push: 'c4' } }
ck C4.new.l.join('|'), 'c4', 'a class BUILD replaces the role BUILD it overrides';

# A role composed THROUGH another role is reached, and still runs once.
role R5a { has @.l is rw; method TWEAK { @!l.push: 'r5a' } }
role R5b does R5a { }
class C5 does R5b { }
ck C5.new.l.join('|'), 'r5a', 'a role composed through a role runs once';

# A role punned into a class by constructing it is still its own class.
role R6 { has @.l is rw; method TWEAK { @!l.push: 'r6' } }
ck R6.new.l.join('|'), 'r6', 'a punned role runs its own TWEAK';

# Issue #72, undamaged: a parent class's own BUILD still runs, least-derived
# first, and each class's BUILD is followed by its OWN TWEAK.
class P7 { has @.l is rw; submethod BUILD { @!l.push: 'P7b' }; method TWEAK { @!l.push: 'P7t' } }
class C7 is P7 { submethod BUILD { self.l.push: 'C7b' }; method TWEAK { self.l.push: 'C7t' } }
ck C7.new.l.join('|'), 'P7b|P7t|C7b|C7t', 'parent and child hooks interleave per class';

# A parent that composes a role, and a child that declares its own: the role's
# hook belongs to the PARENT's turn, so both fire, in that order.
role R8 { method TWEAK { self.l.push: 'r8' } }
class P8 does R8 { has @.l is rw; }
class C8 is P8 { method TWEAK { self.l.push: 'c8' } }
ck C8.new.l.join('|'), 'r8|c8', "a parent's composed TWEAK runs on the parent's turn";

# Role attributes are flattened into the composer, so `is required` on one is
# still enforced — the per-class step the roles used to sit in the chain for.
role R9 { has $.needed is required; }
class C9 does R9 { }
ck C9.new(needed => 7).needed.Str, '7', 'a required role attribute is accepted';
ck (try { C9.new; 'NO-THROW' } // 'threw'), 'threw', 'a required role attribute is still enforced';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
