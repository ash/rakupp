# A module that declares a trait of its own and then imports NativeLibs'
# shape, which hands back `&trait_mod:<is>` from its EXPORT sub. Companion to
# t/regression/issue-211-trait-is-candidates.raku.
unit module RakuppOwnTraitUser;
role Zapped { method zapped { True } }
multi trait_mod:<is>(Routine $r, :$zapped! --> Nil) { $r does Zapped }
use RakuppNativeReexport;
sub before() is zapped { }
sub own-trait-applied() is export { ?&before.?zapped }
