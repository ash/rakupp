# Regression: two parse gaps that made whole modules uncompilable.
#
# 1. A SIGILLESS declarator could not carry traits. Color::Names writes
#    `my constant \COLORS is export(:colors) = %( … )`; the trait stayed in the
#    token stream, so the `=` looked like an assignment TO `is export(:colors)`
#    and the module died with "Target is not assignable". Two paths needed it —
#    the bare `my \x` declarator and the typed `my Mu \x` one.
#
# 2. The container-typed assignment operators `=$=`, `=@=` and `=%=` were not
#    lexed at all: `=%=` split into `=` and a modulo-assign, giving "unexpected
#    operator in term position". They pick the assignment SEMANTICS explicitly
#    rather than taking them from the target's sigil.
#
# KNOWN INCOMPLETE, deliberately not asserted here: when the operator's sigil
# DIFFERS from the target's, Rakudo builds the value per the operator and then
# stores it per the target, so `my $x =@= (1,2,3)` is `$[1, 2, 3]` (an itemised
# Array in a scalar) and `my @a =$= (1,2,3)` is `[(1,2,3),]` (one element). We
# get the value right and the itemisation wrong. The matching-sigil cases below,
# and the sigilless case every real user of these operators writes, are correct.
# Contract: exit 0 + last line PASS.
my @fail;

# ---- 1. traits on a sigilless declarator -----------------------------------
my constant \PLAIN = 5;
@fail.push("plain ({PLAIN})") unless PLAIN == 5;

my constant \TRAITED is export = 7;
@fail.push("traited ({TRAITED})") unless TRAITED == 7;

my constant \ARGS is export( :colors ) = 9;
@fail.push("traited with args ({ARGS})") unless ARGS == 9;

# a sigiled constant with a trait kept working
my constant SIGILED is export = 13;
@fail.push("sigiled ({SIGILED})") unless SIGILED == 13;

# ---- 2. the container-typed assignment operators ---------------------------
# matching sigils: the operator names what the target already is
my @arr =@= (1, 2, 3);
@fail.push("=\@= to \@ ({@arr.raku})") unless @arr.raku eq '[1, 2, 3]';
my %hash =%= (a => 1, b => 2);
@fail.push("=%= to % ({%hash.raku})") unless %hash.raku eq '{:a(1), :b(2)}';
my $item =$= 5;
@fail.push("=\$= to \$ ({$item.raku})") unless $item.raku eq '5';

# the shape real code uses: a sigilless target, which has no sigil to disagree
my constant \HASH =%= %( a => 1, b => 2 );
@fail.push("sigilless =%= ({HASH.raku})") unless HASH.raku eq '{:a(1), :b(2)}';
@fail.push('sigilless =%= lookup') unless HASH<a> == 1;
my \LIST =@= (1, 2, 3);
@fail.push("sigilless =\@= ({LIST.raku})") unless LIST.raku eq '[1, 2, 3]';

# …and both together, which is the line Color::Names actually ships
my constant \COLORS is export( :colors ) =%= %( red => 1, blue => 2 );
@fail.push("the real shape ({COLORS.raku})") unless COLORS<red> == 1 && COLORS<blue> == 2;

# `%=` still means modulo-assign
my $m = 7; $m %= 2;
@fail.push("modassign ({$m})") unless $m == 1;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
