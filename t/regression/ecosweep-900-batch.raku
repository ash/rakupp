# Regression: the ecosystem batch toward 900 green distributions. Every row here
# is an engine bug that Text::CSV — the dist eight others in the sweep wait on —
# walked into, one test file at a time. None of them is about CSV.
#
#   * Slang::Tuxic is applied by the ENGINE (a call's argument list may stand
#     off from its name); rakupp has no grammar to mix a slang into
#   * `.subst-mutate` never interpolated `$var` atoms in its pattern
#   * `[ f() ]` spreads a call's Array result, and an Iterable object's iterator
#   * `$fh.chomp = False`, an explicit `nl-in`, the default `nl-in` pair, `.seek`
#   * `~*` and friends honour a user `method Str` / `method Bool`
#   * `NaN.Int` is a Failure, not a throw
#   * `is rw` takes part in MULTI DISPATCH
#   * `$!attr := $rw-param` aliases the caller's container
#   * an exception's `.Str`/`.gist` is its message, CX:: included
#   * a flattening slurpy stops at an Array; `take` splices a Slip
#   * an endless Range inside an index list clamps to the array
#   * the `anon` declarator
#   * `eq`/`ne`/`lt`… compare an object by its own `method Str`
#   * `.append` flattens a sole Range
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- .subst-mutate interpolates its pattern -----------------------------
# A distinct marker per row: the same replacement text in two rows could not
# tell a working pattern from one that matched nothing.
my $q = '"'; my $e = '"';
my $t1 = '"';
$t1.subst-mutate(/( $q | $e )/, { "$e$0" }, :g);
ck $t1, '""', 'subst-mutate resolves $var atoms in the pattern';

my $t2 = 'a"b"c';
$t2.subst-mutate(/( $q | $e )/, { "SM3$0" }, :g);
ck $t2, 'aSM3"bSM3"c', '…and does it for every occurrence under :g';

my $t3 = 'x-y';
$t3.subst-mutate(/(\-)/, { "SM4" }, :g);
ck $t3, 'xSM4y', 'a literal pattern still substitutes';

# ---- the array composer's one-arg rule ----------------------------------
sub arr9() { my @a = 'SP5', 'SP6'; @a }
class Box9 { method vals() { ('SP7', 'SP8').Array } }
ck [arr9()],           ['SP5', 'SP6'], 'a sub returning an Array spreads in [ ]';
ck [Box9.new.vals],    ['SP7', 'SP8'], '…and so does a method returning one';
ck [(1, 2).Array],     [1, 2],         '…and a .Array coercion';
my @nest9 = ['N1', 'N2'], ['N3', 'N4'];
ck [@nest9[0]].elems,  1,              'an array ELEMENT is a container: it stays whole';
my $held9 = my @h9 = 'H1', 'H2';
ck [$held9].elems,     1,              '…as does a Scalar holding an Array';
ck [arr9(),].elems,    1,              'a trailing comma takes it out of the one-arg rule';

class Iter9 does Iterable {
    has $.a = 'IT1';
    has $.b = 'IT2';
    method iterator { $[ $!a, $!b ].iterator }
}
ck [Iter9.new], ['IT1', 'IT2'], 'an Iterable object spreads through its own iterator';
class Plain9 { has $.x = 'PL1' }
ck [Plain9.new].elems, 1, '…and one that is not Iterable does not';

# ---- IO::Handle line state ---------------------------------------------
my $tmp9 = $*TMPDIR.add("rakupp-eco900-{$*PID}.txt");
$tmp9.spurt("L1\nL2\n");
{
    my $fh = open $tmp9, :r;
    $fh.chomp = False;
    ck $fh.get, "L1\n", 'chomp = False hands back the line WITH its terminator';
    ck $fh.get, "L2\n", '…for every line';
    $fh.close;
}
{
    my $fh = open $tmp9, :r;
    ck $fh.get,  'L1', 'the default still chomps';
    ck $fh.tell, 3,    '.tell is the byte offset after that line';
    $fh.seek(0, SeekFromBeginning);
    ck $fh.get,  'L1', '.seek rewinds the handle';
    $fh.close;
}
{
    my $fh = open $tmp9, :r;
    ck $fh.nl-in.List, ("\n", "\r\n"), 'the default nl-in is Rakudo\'s two-element list';
    $fh.close;
}
{
    # An explicit "\n" separator means a lone CR is DATA, not part of a terminator.
    $tmp9.spurt("C1\rC2\n");
    my $fh = open $tmp9, :r;
    $fh.nl-in = "\n";
    ck $fh.get, "C1\rC2", 'an explicit nl-in splits on exactly that string';
    $fh.close;
}
$tmp9.unlink;

# ---- a class deriving IO::Handle inherits its state ----------------------
class Handle9 is IO::Handle {
    has @!c;
    method print(*@w) { @!c.push('<' ~ @w.join('') ~ '>'); self }
    method get { @!c ?? @!c.shift !! Str }
}
{
    my $h = Handle9.new;
    ck $h.chomp,  True,              'a derived handle inherits chomp (True)';
    ck $h.nl-out, "\n",              '…and nl-out';
    ck $h.nl-in.List, ("\n", "\r\n"), '…and nl-in';
    $h.chomp = False;
    ck $h.chomp,  False,             '…and chomp is writable state';
}

# ---- curried prefix ops honour a user Str/Bool --------------------------
class Field9 {
    has Str $.text;
    method Str  { $!text }
    method Bool { $!text.defined && $!text ne '' }
}
my @f9 = Field9.new(text => 'F1'), Field9.new(text => 'F2');
ck @f9.map(~*).List, ('F1', 'F2'), '~* stringifies through a user method Str';
ck @f9».Str.List,    ('F1', 'F2'), '…agreeing with the hyper method call';
my @b9 = Field9.new(text => ''), Field9.new(text => 'B2');
ck @b9.map(?*).List, (False, True), '?* boolifies through a user method Bool';
ck ~Field9.new(text => 'F3'), 'F3', 'and prefix ~ alone still does';

# An undefined result from a user `Str` is "no string", not "".
class Blank9 { has Str $.text; method Str { $!text } }
ck (~Blank9.new).defined,           False, 'a user Str answering a type object keeps it';
ck [Blank9.new].map(~*)[0].defined, False, '…through the curried form too';

# ---- object string comparison -------------------------------------------
my $empty9 = Field9.new(text => '');
my $space9 = Field9.new(text => ' ');
ck ($empty9 eq ''),      True,  'eq compares an object by its own Str';
ck ($empty9 ne ''),      False, '…and so does ne';
ck ($space9 ne ''),      True,  '…for a non-empty one';
ck ($empty9 lt 'A'),     True,  '…and the ordering operators';
ck (Field9.new(text => 'zz') gt 'aa'), True, '…in both directions';

# ---- NaN/Inf to Int is a Failure ----------------------------------------
{
    my $n = NaN.Int;
    ck $n.defined, False, 'NaN.Int is an undefined Failure, not a throw';
    ck ($n.WHAT.^name eq 'Failure'), True, '…of type Failure';
    my $i = Inf.Int;
    ck $i.defined, False, 'Inf.Int likewise';
}

# ---- `is rw` takes part in multi dispatch --------------------------------
class Acc9 { has $.v is rw }
multi sub pick9($x is rw) { 'RW' }
multi sub pick9($x)       { 'VAL' }
my $acc9 = Acc9.new(v => 1);
my Str $var9 = 'q';
ck pick9($acc9.v),   'RW',  'an `is rw` accessor reaches the rw candidate';
ck pick9($var9),     'RW',  '…as does a plain variable';
ck pick9($var9.Str), 'VAL', 'a coercion is a VALUE: it falls through to the other';
ck pick9($var9.uc),  'VAL', '…and so is any other method result';
ck pick9('lit'),     'VAL', '…and a literal';

# The shape that made this matter: the rw candidate delegates to its sibling.
class Mk9 {
    has Str $.tag is rw;
    multi method new(Str $s! is rw, *%init) { my \o = self.new($s.Str, |%init); o }
    multi method new(Str $s!, *%init) { my \o = self.bless; o.tag = "MK:$s"; o }
}
ck Mk9.new('x').tag, 'MK:x', 'a literal reaches the non-rw candidate, not itself';
my Str $mkv9 = 'y';
ck Mk9.new($mkv9).tag, 'MK:y', '…and a container goes through the rw one and lands';

# ---- an `is rw` parameter bound into an attribute ------------------------
class Bind9 {
    has Str $!out;
    has @!c;
    method bind-str(Str $s is rw) { $!out := $s }
    method add($x) { @!c.push($x); self }
    method Str { @!c.join('') }
    method close { $!out.defined and $!out = ~self }
}
{
    my Str $sink9 = '';
    my $b = Bind9.new;
    $b.bind-str($sink9);
    $b.add('BN1').add('BN2');
    $b.close;
    ck $sink9, 'BN1BN2', 'an attribute bound to an `is rw` param writes to the caller';
}

# ---- an exception stringifies to its message ----------------------------
{
    my @warn9;
    {
        warn 'WM1';
        warn 'WM2';
        CONTROL { when CX::Warn { @warn9.push: $_.Str; .resume } };
    }
    ck @warn9, ['WM1', 'WM2'], 'CX::Warn.Str is the warning message';
}
{
    my $caught9;
    try { die 'DM1' };
    $caught9 = $!;
    ck $caught9.Str, 'DM1', 'and an X:: exception says the same thing';
}

# ---- a flattening slurpy stops at an Array ------------------------------
sub slurp9(*@a) { @a }
ck slurp9([['S1', 'S2'], ['S3', 'S4']]).elems, 2,
   'a flattening slurpy dissolves the outer Array and keeps the inner ones';
ck slurp9([['S5', 'S6'], ['S7', 'S8']])[0], ['S5', 'S6'],
   '…so each element is still the Array it was';
ck slurp9(('S9', ('SA', 'SB'))).elems, 3,
   'nested LISTS flatten all the way — they carry no containers';
ck slurp9([['SC', 'SD'], ['SE', 'SF']], 'SG').elems, 3,
   '…and a following argument is its own element';
ck slurp9($['SH', 'SI']).elems, 1, 'an itemized array stays one element';

# ---- take splices a Slip -------------------------------------------------
{
    my @g9 = gather { take ((1..3).Slip); take ((7..8).Slip) };
    ck @g9, [1, 2, 3, 7, 8], 'take of a Slip contributes its ELEMENTS';
    my Int @typed9 = gather { take ((4..5).Slip) };
    ck @typed9.List, (4, 5), '…which is what lets a typed array accept them';
}

# ---- an endless Range inside an index list clamps ------------------------
{
    my @idx9 = <e0 e1 e2 e3 e4 e5>;
    ck @idx9[2..Inf].List, ('e2', 'e3', 'e4', 'e5'), 'an endless range slice stops at the end';
    # .flat, because the two engines still disagree about the SHAPE a range
    # inside an index list produces (Rakudo nests it); the point here is that
    # neither pads the slice out to ten thousand elements.
    ck @idx9[1, 3..Inf].flat.List, ('e1', 'e3', 'e4', 'e5'), '…and so does one INSIDE an index list';
    ck @idx9[1, 3..*].flat.List,   ('e1', 'e3', 'e4', 'e5'), '…spelled with a Whatever';
    ck @idx9[10..Inf].elems,   0, 'one that starts past the end is empty';
}

# ---- the `anon` declarator ----------------------------------------------
{
    my $an9 = anon sub (@a, *@b) { 'AN1:' ~ @a.join(',') ~ '/' ~ @b.join(',') };
    ck $an9(['p', 'q'], 'r', 's'), 'AN1:p,q/r,s', 'anon sub is a term with a value';
    my $ac9 = anon class { method m { 'AN2' } };
    ck $ac9.new.m, 'AN2', 'anon class likewise';
}

# ---- .append flattens a sole Range ---------------------------------------
{
    my Int @ap9;
    @ap9.append: 2 .. 4;
    ck @ap9.List, (2, 3, 4), '.append contributes a sole Range as its VALUES';
    my @two9;
    @two9.append((1, 2), (3, 4));
    ck @two9.elems, 2, '…while two arguments are two elements, as before';
    my @pu9;
    @pu9.push(2 .. 4);
    ck @pu9.elems, 1, 'and .push still flattens nothing';
}

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
