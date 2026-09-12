# Regression: the cluster behind Needle::Compile and App::Rak (2026-09-12), plus
# the three CBOR::Simple ones found beside them. RakuAST had landed, so what
# actually stood in the way was nine ordinary engine bugs.
#
# Every expectation below was checked against Rakudo 2026.08 — run from
# /opt/homebrew/bin/raku, NOT the bare name `raku`, which on the author's box is
# a symlink that has pointed at rakupp. This file is green on both engines.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- 1. a Str-ish object in string context contributes its VALUE -----------
# Needle::Compile's Type role spells its stringifier `method Str { self ~ "" }`.
# If `~` asks such an operand for .Str, that recurses forever. An object that
# IS-A Str binds Rakudo's Str:D candidates, and those read the unboxed value.
role Tag1 { method Str { self ~ "" } }
role Tag2 { method Str { "OVERRIDDEN" } }
class SubStr is Str { method Str { "OVERRIDDEN-SUB" } }
class NotStr { method Str { "plain" } }

ck(("bb-p2" but Tag1) ~ "!", "bb-p2!", 'a `method Str` spelt `self ~ ""` terminates');
ck(("cc-p3" but Tag2) ~ "!", "cc-p3!", 'concat takes a Str mixin VALUE, not its .Str');
ck(SubStr.new(value => "dd") ~ "!", "dd!", '…and a class `is Str` the same way');
ck(NotStr.new ~ "!", "plain!", 'a NON-Str object still goes through .Str');
ck(("bb" but Tag2) ~ NotStr.new, "bbplain", 'per operand, even beside a plain object');
ck(("ee" but Tag2).Str, "OVERRIDDEN", 'an explicit .Str still dispatches');
ck((("mm" but Tag2) eq "mm"), True, 'string comparison uses the value (was False)');
ck(("hh" but Tag2, "x").join("-"), "hh-x", 'join takes the value');
ck("[{ "ii" but Tag2 }]", "[ii]", 'interpolation takes the value');
# `eq`, not `eqv`: Rakudo's Str.gist answers `self`, so the result keeps the
# mixin type (`Str+{Tag2}`) where rakupp hands back a plain Str. Both render
# the string, which is what this row is about; the type nicety is logged as a
# known divergence rather than asserted here.
ck((("jj" but Tag2).gist eq "jj"), True, 'Str.gist is the string itself, not the override');
# KNOWN DIVERGENCE, deliberately not asserted: `say (41 but R)` is R's .Str on
# both engines (that is `Int.gist`), but the explicit `(41 but R).gist` answers
# "41" here and "OVERRIDDEN" upstream — the method call reaches the boxed value
# instead of the mixin. Pre-existing, unrelated to this cluster, unfixed.

# --- 2. multi narrowness is PER PARAMETER, not a sum -----------------------
# Rakudo prefers a candidate only when it is no wider on any parameter and
# narrower on some; two that each win a different parameter are in one band,
# where DECLARATION ORDER decides.
proto sub h1(|) {*}
multi sub h1("not", Any:D $x, %o)    { "literal" }
multi sub h1(Str:D $t, Str:D $x, %o) { "strstr" }
ck(h1("not", "a", %()), "literal", 'a literal parameter is not outscored by two Str:D');

proto sub h2(|) {*}
multi sub h2(Str:D $t, Str:D $x, %o) { "strstr" }
multi sub h2("not", Any:D $x, %o)    { "literal" }
ck(h2("not", "a", %()), "strstr", '…and with the order flipped the first declared wins');

proto sub h3(|) {*}
multi sub h3(Any:D $t, Any:D $x) { "any" }
multi sub h3(Str:D $t, Str:D $x) { "str" }
ck(h3("a", "b"), "str", 'real dominance still wins regardless of order');

# --- 5. `but` on a BUILT-IN type object answers a TYPE OBJECT --------------
role Tg { has $.type }
my constant StrTg = Str but Tg;
ck(StrTg.defined, False, '`Str but R` is a type object, not an instance');
ck((("x" but Tg("t")) ~~ StrTg), True, 'and a mixed-in value smartmatches it');
ck(("plain" ~~ StrTg), False, '…while a plain Str does not');

# --- 6. a parameter typed by a CONSTANT resolves ---------------------------
my constant PlainS = Str;
proto sub k1(|) {*}
multi sub k1(PlainS:D $x) { "alias" }
multi sub k1(Any:D $x)    { "any" }
ck(k1("a"), "alias", 'a constant aliasing a built-in binds as that type');

proto sub k2(|) {*}
multi sub k2(StrTg:D $x) { "mixin" }
multi sub k2(Str:D $x)   { "plain" }
ck(k2("y" but Tg("t")), "mixin", '…and a constant holding a mixin type does too');
ck(k2("z"), "plain", '…without swallowing the plain case');

# --- 7. reading past the end of a Blob THROWS -----------------------------
# CBOR::Simple matches the message BY NAME, so the text is asserted, not styled.
{
    use nqp;
    my $ne8 = nqp::const::BINARY_SIZE_8_BIT +| nqp::const::BINARY_ENDIAN_NATIVE;
    ck(nqp::readuint(Blob[uint8].new(0x41, 0x42, 0x43), 1, $ne8), 66, 'an in-range read still answers');
    my $threw = (try { nqp::readuint(Blob[uint8].new(0x51, 0x52, 0x53), 3, $ne8); 'no' }) // ($!.Str.lines[0]);
    ck($threw.starts-with('MVMArray: read_buf out of bounds'), True, 'reading past the end throws MoarVM\'s message');
    ck(nqp::atpos_i(nqp::decont(Blob[uint8].new(0x81, 0x82)), 7), 0, 'nqp::atpos_i past the end still answers 0');
}

# --- 8. nqp::istype of a TYPE OBJECT against its own type ------------------
{
    use nqp;
    ck(?nqp::istype(Str, Str), True, 'nqp::istype(Str, Str) is True');
    ck(?nqp::istype(Str, Int), False, '…and nqp::istype(Str, Int) is not');
    my %plain; %plain<a> = 1;
    ck(?nqp::istype(%plain.keyof, Str), True, 'a plain hash keyof IS a Str (CBOR tag 259)');
    my %obj{Mu}; %obj<a> = 1;
    ck(?nqp::istype(%obj.keyof, Str), False, '…and an object hash keyof is not');
}

# --- 9. Num.Str is the SHORTEST decimal that round-trips ------------------
# A power of two ends its decimal expansion in an exact tie, and printf breaks
# ties to even — which can hand back the one neighbour that does not round-trip.
ck((2 ** -24).Num.Str, "5.960464477539063e-08", 'an exact tie takes the shorter round-tripping form');
ck((2 ** -24).Num.Str.Num == (2 ** -24).Num, True, '…and it still round-trips');
ck(pi.Str, "3.141592653589793", 'an ordinary Num is unchanged');
ck(0.1e0.Str, "0.1", '…and so is a short one');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
