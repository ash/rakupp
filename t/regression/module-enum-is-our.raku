# Regression: an enum a module declares outside any package was visible only
# inside that module. `enum HTTP::HPACK::Indexing <Indexed …>` at the top of
# HTTP/HPACK.rakumod, and its test's `HTTP::HPACK::Indexing::Indexed`, died
# "Could not find symbol '&Indexed' in 'GLOBAL::HTTP::HPACK::Indexing'", which
# stopped `rakupp install cro`. An enum is `our`: its type and its members
# under their qualified names are global.
#
# The same for `enum G::Mode` inside `unit module G`: that is G's own Mode,
# which rakupp filed as G::G::Mode.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppEnumOur;
use RakuppEnumOurUnit;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck RakuppEnumOur::Indexing::NotIndexed.Str, 'NotIndexed', 'a qualified member';
ck RakuppEnumOur::Indexing.^name, 'RakuppEnumOur::Indexing', 'the type';
ck RakuppEnumOur::Indexing.enums.keys.sort.List, <Indexed NeverIndexed NotIndexed>, 'its members';
ck RakuppEnumOurPlain::PlainB.value, 1, 'an unqualified enum by its qualified member';

sub header($indexing = RakuppEnumOur::Indexing::NeverIndexed) {
    RakuppEnumOur::Header.new(:$indexing)
}
ck header().indexing, RakuppEnumOur::Indexing::NeverIndexed, 'as a default value (HPACK)';
ck RakuppEnumOur::Header.new.indexing, RakuppEnumOur::Indexing::Indexed, "the module's own default";
ck RakuppEnumOur::Header.new.indexing ~~ RakuppEnumOur::Indexing, True, 'a member smartmatches its type';

ck RakuppEnumOurUnit::Mode::On.Str, 'On', "the unit's own qualified enum";
ck RakuppEnumOurUnit::Level::High.Str, 'High', "the unit's bare enum, under the unit";

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
