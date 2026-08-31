#?requires BSON::Simple
# Regression: BSON::Simple encodes and decodes correctly end-to-end — the dist
# INSTALLS (2d04edb) and its brackets parse (6b7dc24), but nothing until now ran
# its codec. Every hex string below is the Rakudo 2026.08 answer for the same
# call, so this file passes under Rakudo too.
#
# What it caught: `DateTime ~~ Associative` was True here and False on Rakudo,
# because a DateTime rides on VT::Hash. BSON::Simple's encode-element tests
# `when Associative` BEFORE `when Dateish`, so every DateTime was written as a
# sub-document of its own accessors (year/month/day/... — 0x66 bytes) instead of
# the 8-byte BSON datetime (type 0x09). Silent, and byte-wrong on the wire.
#
# Contract: exit 0 + last line PASS.
use BSON::Simple;
use Hash::Ordered;

my @fail;
sub hx($b) { $b.list.fmt('%02x', '') }
sub enc($v) { hx(bson-encode($v)) }
sub is-hex($got, $want, $what) { @fail.push("$what: got $got, want $want") unless $got eq $want }
sub ok($cond, $what) { @fail.push($what) unless $cond }

# --- the spec's own vectors, byte for byte ------------------------------------
# int32 elements are type 0x10, strings 0x02, documents 0x03, arrays 0x04; each
# document carries its own little-endian length and a trailing NUL.
is-hex enc({}),                  '0500000000',                                     'empty document';
is-hex enc({hello => 'world'}),  '160000000268656c6c6f0006000000776f726c640000',   'the canonical hello/world';
is-hex enc({BSON => ['awesome', 5.05e0, 1986]}),
       '310000000442534f4e002600000002300008000000617765736f6d65000131003333333333331440103200c20700000000',
       'the bsonspec.org array example';
is-hex enc({n => 42}),           '0c000000106e002a00000000',                       'a small Int is an int32';
is-hex enc({n => -1}),           '0c000000106e00ffffffff00',                       'a negative int32';
is-hex enc({n => 5.05e0}),       '10000000016e00333333333333144000',               'a Num is a double';
is-hex enc({r => 1/2}),          '10000000017200000000000000e03f00',               'a Rat encodes through .Num';
is-hex enc({b => True}),         '090000000862000100',                             'True';
is-hex enc({b => False}),        '090000000862000000',                             'False';
is-hex enc({v => Any}),          '080000000a760000',                               'an undefined Any is BSON null';
is-hex enc({a => [1, 2]}),       '1b0000000461001300000010300001000000103100020000000000',
       'an array is a document with stringified indices';
is-hex enc({d => {x => 1}}),     '140000000364000c000000107800010000000000',       'a nested document';
is-hex enc({b => buf8.new(1, 2, 3)}), '10000000056200030000000001020300',          'a Blob is generic binary';
is-hex enc({'ключ' => 'значение'}),
       '2400000002d0bad0bbd18ed1870011000000d0b7d0bdd0b0d187d0b5d0bdd0b8d0b50000',
       'non-ASCII keys and values are UTF-8, byte-counted';

# --- the int32/int64 boundary decides the element type ------------------------
is-hex enc({n =>  2147483647}), '0c000000106e00ffffff7f00',         'int32 max stays an int32';
is-hex enc({n =>  2147483648}), '10000000126e00000000800000000000', 'one past it promotes to int64';
is-hex enc({n => -2147483648}), '0c000000106e000000008000',         'int32 min stays an int32';
ok bson-decode(bson-encode({n => 9223372036854775807}))<n> == 9223372036854775807, 'int64 max round-trips';

# --- a DateTime is a BSON datetime, NOT a sub-document ------------------------
# The regression this file was written for. 0x09 is BSON_Datetime; the payload
# is int64 milliseconds since the epoch.
{
    my $dt = DateTime.new('2012-12-24T12:15:30Z');
    is-hex enc({d => $dt}),                  '10000000096400d0d6d6cc3b01000000', 'a DateTime is type 0x09';
    is-hex enc({d => Date.new(2012, 12, 24)}), '10000000096400007835ca3b01000000', 'and so is a Date';
    ok !(DateTime.now ~~ Associative), 'a DateTime does not do Associative';
    ok !(Date.today   ~~ Associative), 'nor does a Date';
    ok DateTime.now ~~ Dateish && Date.today ~~ Dateish, 'both still do Dateish';
    # decode hands back an Instant on the same clock
    my $back = bson-decode(bson-encode({d => $dt}))<d>;
    ok $back ~~ Instant && $back.to-posix[0] == 1356351330, 'and it decodes to the same instant';
}

# --- the special types survive the round trip --------------------------------
{
    my $oid = BSON::Simple::ObjectID.new('0102030405060708090a0b0c');
    is-hex enc({o => $oid}), '14000000076f000102030405060708090a0b0c00', 'an ObjectID is 12 raw bytes';
    my $o-back = bson-decode(bson-encode({o => $oid}))<o>;
    ok $o-back ~~ BSON::Simple::ObjectID && hx($o-back.id) eq '0102030405060708090a0b0c', 'ObjectID round-trips';

    is-hex enc({k => MinKey}), '08000000ff6b0000', 'MinKey is type 0xff';
    is-hex enc({k => MaxKey}), '080000007f6b0000', 'MaxKey is type 0x7f';
    ok bson-decode(bson-encode({k => MinKey}))<k> === MinKey, 'MinKey round-trips by identity';
    ok bson-decode(bson-encode({k => MaxKey}))<k> === MaxKey, 'MaxKey round-trips by identity';

    my $bin = BSON::Simple::Binary.new(4, hex => '00112233');
    is-hex enc({b => $bin}), '1100000005620004000000040011223300', 'a subtyped Binary keeps its subtype byte';
    my $b-back = bson-decode(bson-encode({b => $bin}))<b>;
    ok $b-back.subtype == 4 && hx($b-back.content) eq '00112233', 'Binary round-trips subtype and content';

    my $re = BSON::Simple::PCRE_Regex.new(pattern => 'ab', options => 'ix');
    is-hex enc({r => $re}), '0e0000000b720061620069780000', 'a regex is two cstrings';
    my $r-back = bson-decode(bson-encode({r => $re}))<r>;
    ok $r-back.pattern eq 'ab' && $r-back.options eq 'ix', 'regex round-trips';

    my $ts = BSON::Simple::Timestamp.new(i => 7, t => 9);
    my $t-back = bson-decode(bson-encode({t => $ts}))<t>;
    ok $t-back.i == 7 && $t-back.t == 9, 'a Timestamp round-trips both halves';

    is-hex enc({j => BSON::Simple::JSCode.new(code => 'x=1')}), '100000000d6a0004000000783d310000', 'JavaScript is type 0x0d';
    ok bson-decode(bson-encode({j => BSON::Simple::JSCode.new(code => 'x=1')}))<j>.code eq 'x=1', 'JSCode round-trips';

    is-hex enc({s => BSON::Simple::Symbol.new(value => 'sy')}), '0f0000000e73000300000073790000', 'a Symbol is type 0x0e';
    is-hex enc({n => BSON::Simple::Int32.new(7)}), '0c000000106e000700000000',         'an explicit Int32 stays 32-bit';
    is-hex enc({n => BSON::Simple::Int64.new(7)}), '10000000126e00070000000000000000', 'an explicit Int64 widens';
}

# --- key order is the ITERATION order of what went in -------------------------
# BSON documents are ordered, so an ordered hash must come back in its own order
# (a plain Raku Hash has no order to preserve — on either engine).
{
    my $o = Hash::Ordered.new;
    $o<z> = 1; $o<a> = 2; $o<m> = 3;
    is-hex hx(bson-encode($o)), '1a000000107a000100000010610002000000106d000300000000', 'an ordered hash encodes in its order';
    my $back = bson-decode(bson-encode($o));
    ok $back ~~ Hash::Ordered,          'decode hands back an ordered hash by default';
    ok $back.keys.join(',') eq 'z,a,m', 'and the order survived the round trip';
    ok $back<z> == 1 && $back<a> == 2 && $back<m> == 3, 'with the right values';
}

# --- the streaming form: many documents in one buffer -------------------------
{
    my $buf = buf8.new;
    my $pos = 0;
    bson-encode({a => 1}, $pos, $buf);
    bson-encode({b => 2}, $pos, $buf);
    is-hex hx($buf), '0c00000010610001000000000c0000001062000200000000', 'two documents append into one buffer';
    ok $pos == 24, 'and $pos is left past both';

    my $read = 0;
    my $d1 = bson-decode($buf, $read);
    my $d2 = bson-decode($buf, $read);
    ok $d1<a> == 1 && $d2<b> == 2 && $read == 24, 'and they decode back one at a time';
}

# --- round trips through nested structure ------------------------------------
{
    my $rt = bson-decode(bson-encode({outer => {name => 'Tokyo', n => 3},
                                      list  => [1, 'two', True]}));
    ok $rt<outer><name> eq 'Tokyo' && $rt<outer><n> == 3, 'a nested document round-trips';
    ok $rt<list> ~~ Positional && $rt<list>[1] eq 'two' && $rt<list>[2] === True, 'and a mixed array';
    ok bson-decode(bson-encode({'ключ' => 'значение'}))<ключ> eq 'значение', 'and non-ASCII text';
    ok bson-decode(bson-encode({v => Any}))<v> === Any, 'BSON null decodes to Any';
    ok bson-decode(bson-encode({n => 5.05e0}))<n> ~~ Num, 'a double decodes to Num';
}

# --- the PROCESS switches change what decode builds ---------------------------
{
    my $*BSON_SIMPLE_PLAIN_HASHES = True;
    my $h = bson-decode(bson-encode({a => 1}));
    ok $h ~~ Hash && !($h ~~ Hash::Ordered), 'PLAIN_HASHES gives an ordinary Hash';
    ok $h<a> == 1, 'that still holds the value';
}
{
    my $*BSON_SIMPLE_PLAIN_BLOBS = True;
    # (.^name is 'Buf[uint8]' on Rakudo and 'Buf' here — the TYPE is what matters)
    my $b = bson-decode(bson-encode({b => buf8.new(1, 2)}))<b>;
    ok $b ~~ Buf && $b.list eqv (1, 2), 'PLAIN_BLOBS gives a bare Buf';
}
ok bson-decode(bson-encode({b => buf8.new(1, 2)}))<b> ~~ BSON::Simple::Binary,
   'while the default wraps binary in BSON::Simple::Binary';

# --- the exported utility -----------------------------------------------------
is-hex hx(hex-decode('deadbeef')), 'deadbeef', 'hex-decode parses a hex string to bytes';
ok hex-decode('ff').list eqv (255,), 'one byte at a time';

if @fail { note "FAILED: @fail[]"; say 'FAIL'; exit 1 }
say 'PASS';
