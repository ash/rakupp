# Regression: nominal type checks agree with Rakudo's type graph, in `~~`,
# in parameter binding (and so multi dispatch), and in typed `$` variables
# and attributes. Found while fixing issue #110 (constructor type checks):
#   - `A.new ~~ Cool` was True for ANY class; so were a Pair, a Set, a Sub,
#     a Blob, an ObjAt, a Date, a Promise, an Exception and the Any type;
#   - a List and a Seq bound `Array $p`, and a Seq `List $p`;
#   - a Seq did not bind `PositionalBindFailover`/`Sequence`, and `.WHICH`
#     bound Str and Stringy but not ObjAt;
#   - `Cool $p` refused a Hash, a Map and MY::;
#   - a typed variable checked only Int UInt Num Rat Complex Str Bool and the
#     program's own types: `my Array $x = (1, 2)`, `my Any $y = Mu` and
#     `my Any $z = 1|2` all stored, as did `has Any $.a` given Mu.
# Every expectation here is Rakudo's (2026.09).
# Contract: exit 0 + last line PASS.
my @fail;
sub ck(Bool() $ok, $desc) { @fail.push($desc) unless $ok }
sub dies(&code, $type = Exception) {
    my $got;
    { code(); CATCH { default { $got = $_ } } }
    $got.defined && $got ~~ $type
}

my class A { }
my class B is Int { }
my role R { }
my class C does R { }
my class CAny { has Any $.a }
my class CArr { has Array $.a }
my class CPos { has Positional $.p }

# ---- `~~ Cool` -------------------------------------------------------------
ck !(A.new ~~ Cool),             'user object is no Cool';
ck !(C.new ~~ Cool),             'a role it does does not make it Cool';
ck !(A ~~ Cool),                 'user type object is no Cool';
ck B.new(3) ~~ Cool,             '`is Int` makes it Cool';
ck R ~~ Cool,                    'a role type object answers True (Rakudo quirk)';
ck !((a => 1) ~~ Cool),          'Pair is no Cool';
ck !(set(1) ~~ Cool),            'Set is no Cool';
ck !(sub {} ~~ Cool),            'Sub is no Cool';
ck !(Blob.new(1) ~~ Cool),       'Blob is no Cool';
ck !(1.WHICH ~~ Cool),           'ValueObjAt is no Cool';
ck !(Date.new(2020,1,1) ~~ Cool), 'Date is no Cool';
ck !(Promise.new ~~ Cool),       'Promise is no Cool';
ck !(X::AdHoc.new ~~ Cool),      'Exception is no Cool';
ck !(Any ~~ Cool),               'Any is no Cool';
ck !(Mu ~~ Cool),                'Mu is no Cool';
ck !(Code ~~ Cool),              'Code type object is no Cool';
ck Int ~~ Cool,                  'Int type object is Cool';
ck %(a => 1) ~~ Cool,            'Hash is Cool';
ck (1, 2).map({ $_ }) ~~ Cool,   'Seq is Cool';
ck ('a' ~~ /a/) ~~ Cool,         'Match is Cool';
ck 'x'.IO ~~ Cool,               'IO::Path is Cool';
ck MY:: ~~ Cool,                 'PseudoStash is Cool';

# ---- other smartmatches ------------------------------------------------------
my \seq = (1, 2).map({ $_ });
ck !(seq ~~ List),               'Seq is no List';
ck !(seq ~~ Positional),         'Seq is no Positional to ~~';
ck seq ~~ Sequence,              'Seq does Sequence';
ck seq ~~ PositionalBindFailover, 'Seq does PositionalBindFailover';
ck !(1.WHICH ~~ Stringy),        'ObjAt is no Stringy';

# ---- binding -----------------------------------------------------------------
sub pArr(Array $p) { 1 }
sub pList(List $p) { 1 }
sub pPBF(PositionalBindFailover $p) { 1 }
sub pSeqn(Sequence $p) { 1 }
sub pObjAt(ObjAt $p) { 1 }
sub pStr(Str $p) { 1 }
sub pCool(Cool $p) { 1 }
sub pPos(Positional $p) { 1 }
ck dies({ pArr((1, 2)) }, X::TypeCheck::Binding::Parameter), 'List refused by Array param';
ck dies({ pArr((1, 2).map({ $_ })) }),                        'Seq refused by Array param';
ck dies({ pArr((1, 2).Slip) }),                               'Slip refused by Array param';
ck (try pArr([1, 2])),                                              'Array binds Array param';
ck dies({ pList((1, 2).map({ $_ })) }),                       'Seq refused by List param';
ck (try pList([1, 2])),                                             'Array binds List param';
ck (try pPBF((1, 2).map({ $_ }))),                                  'Seq binds PositionalBindFailover';
ck (try pSeqn((1, 2).map({ $_ }))),                                 'Seq binds Sequence';
ck (try pPos((1, 2).map({ $_ }))),                                  'Seq binds a Positional param';
ck (try pObjAt(1.WHICH)),                                           'ValueObjAt binds ObjAt';
ck (try pObjAt(A.new.WHICH)),                                       'ObjAt binds ObjAt';
ck dies({ pStr(1.WHICH) }),                                   'ObjAt refused by Str param';
ck (try pCool(%(a => 1))),                                          'Hash binds Cool';
ck (try pCool(Map.new((a => 1)))),                                  'Map binds Cool';
ck (try pCool(MY::)),                                               'MY:: binds Cool';
ck (try pCool([1])),                                                'Array binds Cool';
ck dies({ pCool(set(1)) }),                                   'Set refused by Cool';

# ---- dispatch ----------------------------------------------------------------
multi dl(List $) { 'List' }
multi dl(Array $) { 'Array' }
ck ((try dl((1, 2))) // q{}) eq 'List',                       'List goes to the List candidate';
ck ((try dl([1, 2])) // q{}) eq 'Array',                      'Array goes to the Array candidate';
ck dies({ dl((1, 2).map({ $_ })) }, X::Multi::NoMatch), 'a Seq matches neither';
multi dw(Str $) { 'Str' }
multi dw(ObjAt $) { 'ObjAt' }
ck ((try dw(1.WHICH)) // q{}) eq 'ObjAt',                     '.WHICH goes to the ObjAt candidate';
multi dc(Cool $) { 'Cool' }
multi dc(Any $) { 'Any' }
ck ((try dc(%(a => 1))) // q{}) eq 'Cool',                    'a Hash goes to Cool over Any';
ck ((try dc(A.new)) // q{}) eq 'Any',                         'a user object goes to Any';
ck ((try dc(1.WHICH)) // q{}) eq 'Any',                       'an ObjAt goes to Any';
multi dh(Cool $) { 'Cool' }
multi dh(%x) { '%' }
ck ((try dh(%(a => 1))) // q{}) eq '%',                       '%x beats Cool for a Hash';
multi da(Cool $) { 'Cool' }
multi da(Associative $) { 'Associative' }
ck ((try da(%(a => 1))) // q{}) eq 'Associative',             'Associative beats Cool for a Hash';
# a buffer is Stringy, and a buffer type is narrower than Stringy or Positional
sub pStringy(Stringy $p) { 1 }
ck (try pStringy('x'.encode)),                       'utf8 binds Stringy';
ck (try pStringy(Buf.new(1))),                       'Buf binds Stringy';
multi ds(Stringy $) { 'Stringy' }
multi ds(Blob $) { 'Blob' }
ck ((try ds('x'.encode)) // q{}) eq 'Blob',                   'Blob beats Stringy, declared second';
ck ((try ds('x')) // q{}) eq 'Stringy',                       'a Str still goes to Stringy';
multi dp(Positional $) { 'Positional' }
multi dp(Blob $) { 'Blob' }
ck ((try dp('x'.encode)) // q{}) eq 'Blob',                   'Blob beats Positional, declared second';
multi dk(Stringy $) { 'Stringy' }
multi dk(Str $) { 'Str' }
ck ((try dk('x'.encode)) // q{}) eq 'Stringy',                'utf8 goes to Stringy, not Str';

# ---- typed variables -----------------------------------------------------------
my $l = (1, 2);
my $m = Mu;
my $j = 1 | 2;
ck dies({ my Array $x = $l }, X::TypeCheck::Assignment),  'my Array $x = List dies';
ck dies({ my Array $x; $x = $l }, X::TypeCheck::Assignment), '$x = List into Array dies';
ck dies({ my Any $y = $m }, X::TypeCheck::Assignment),    'my Any $y = Mu dies';
ck dies({ my Any $y; $y = $m }, X::TypeCheck::Assignment), '$y = Mu into Any dies';
ck dies({ my Any $z = $j }, X::TypeCheck::Assignment),    'my Any $z = Junction dies';
ck dies({ my Hash $h = 42 }, X::TypeCheck::Assignment),   'my Hash $h = 42 dies';
ck dies({ my Code $c = 42 }, X::TypeCheck::Assignment),   'my Code $c = 42 dies';
ck dies({ my Cool $c = A.new }, X::TypeCheck::Assignment), 'my Cool $c = user object dies';
ck dies({ my Positional $p = (1, 2).map({ $_ }) }, X::TypeCheck::Assignment), 'Seq into Positional var dies';
ck dies({ my Numeric $n = 'a' }, X::TypeCheck::Assignment), 'Str into Numeric var dies';
ck dies({ my List $x = 'x'.NFC }, X::TypeCheck::Assignment), 'Uni into List var dies';
{ my Array $x = [1]; ck $x ~~ Array, 'Array into Array var' }
{ my List $x = [1]; ck $x ~~ Array, 'Array into List var' }
{ my Any $x = 1; ck $x == 1, 'Int into Any var' }
{ my Mu $x = $j; ck $x ~~ Junction, 'Junction into Mu var' }
{ my Cool $x = %(a => 1); ck $x<a> == 1, 'Hash into Cool var' }
{ my Stringy $x = 'x'.encode; ck $x ~~ Blob, 'utf8 into Stringy var' }
{ my Stringy $x = 'x'.NFC; ck $x ~~ Uni, 'Uni into Stringy var' }
{ my Associative $x = MY::; ck $x.defined, 'MY:: into Associative var' }
{ my Positional $x = 1..3; ck $x.elems == 3, 'Range into Positional var' }
{ my Callable $x = { 1 }; ck $x() == 1, 'Block into Callable var' }
{ my Array $x = [1]; $x = Nil; ck $x === Array, 'Nil resets an Array var' }

# ---- typed attributes --------------------------------------------------------
ck dies({ CAny.new(a => Mu) }, X::TypeCheck::Assignment),    'has Any refuses Mu';
ck dies({ CArr.new(a => (1, 2)) }, X::TypeCheck::Assignment), 'has Array refuses a List';
ck dies({ CPos.new(p => (1, 2).map({ $_ })) }, X::TypeCheck::Assignment), 'has Positional refuses a Seq';
ck CArr.new(a => [1]).a ~~ Array,                            'has Array takes an Array';
ck CAny.new(a => 1).a == 1,                                  'has Any takes an Int';

if @fail { say "FAIL: $_" for @fail; say 'FAIL' } else { say 'PASS' }
