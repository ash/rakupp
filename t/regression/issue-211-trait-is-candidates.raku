# Issue #211: a Cro app that used DBIish printed, once per `use NativeLibs`,
#   ===WARNING=== Module NativeLibs EXPORT failed: No such method 'dispatcher'
#   for invocant of type 'Any'
# NativeLibs' EXPORT looks for the `is native` candidate among
# `&trait_mod:<is>.candidates` and re-exports its .dispatcher. The engine's
# traits answered that name only while no user `multi trait_mod:<is>` was in
# scope; Cro::HTTP::Router exports four (`is query`, `is auth`, …), and from
# then on `&trait_mod:<is>` listed those alone, so the search found nothing. A
# user group now lists the engine's candidates ahead of its own, as a Rakudo
# multi joins CORE's group (builtinTraitIsGroup).
#
# Mending it let that EXPORT succeed, and its `'&trait_mod:<is>' => …` was
# BOUND over the importer's own group: a module that declared `is zapped` and
# then said `use NativeLibs` lost the trait without a word (it already did with
# no Cro in sight). An EXPORT sub's multi now joins the importer's group, as an
# exported `multi` does (importJoinsLocalMulti).
#
# The modules in lib/ are Cro's and NativeLibs' shapes, so no ecosystem module
# is needed. Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppParamTraits;
use RakuppNativeReexport;
use RakuppOwnTraitUser;

my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# the report: the re-export runs, and says nothing, after a module's own traits
my $lib = $?FILE.IO.parent.add('lib').Str;
my $p = run $*EXECUTABLE, '-I', $lib, '-e', 'use RakuppParamTraits; use RakuppNativeReexport; print "done"',
            :out, :err;
check 'the re-export after user traits prints nothing on stderr', $p.err.slurp(:close), '';
check '…and the program runs on', $p.out.slurp(:close), 'done';

# what that EXPORT asks of `&trait_mod:<is>`, with user candidates in scope
my $native = &trait_mod:<is>.candidates.first: { .signature ~~ :(Routine, :$native!) };
check '`is native` is a candidate beside user multis', $native.^name, 'Sub';
check '…with a dispatcher to hand on', $native.dispatcher.^name, 'Sub';
check '…that holds it', $native.dispatcher.candidates.grep({ .signature ~~ :(Routine, :$native!) }).elems, 1;
check 'the user candidates are listed too',
      &trait_mod:<is>.candidates.grep({ .signature ~~ :(Parameter:D, :$authed! --> Nil) }).elems, 1;

# the traits the program relies on still apply
check 'an imported `is` trait applies after the re-export',
      -> $x is authed { }.signature.params[0] ~~ RakuppParamTraits::Authed, True;
{   # (a block of its own: a mainline multi would be in the group checked above)
    multi trait_mod:<is>(Routine $r, :$marked! --> Nil) { $r does role { method marked { True } } }
    sub m() is marked { }
    check 'a trait the program declares applies', ?&m.?marked, True;
    check '…and `is native` is still among its candidates',
          &trait_mod:<is>.candidates.first({ .signature ~~ :(Routine, :$native!) }).^name, 'Sub';
}
check 'a module keeps its own trait across the re-export', own-trait-applied(), True;

.say for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
