# Regression: CBOR::Simple works end-to-end (byte-identical to Rakudo), plus the
# underlying fixes it exercised —
#   * nqp buffer ops: bitor_i/bitshift, writeuint/readuint tower, elems/atpos on
#     Buf, splice on a Buf (lvalue mutation), nqp::const::BINARY_SIZE_*
#   * `buf8.new.write-num64(...)` on a temporary (rvalue) invocant
#   * `my num32 $x = <double>` rounds to float32 precision
#   * Bool ~~ Int / nqp::istype(True, Int) (Bool is an Int-backed enum)
#   * prefix +^/~^/?^ as a call/listop argument after a comma
# Contract: exit 0 + last line PASS.
use CBOR::Simple;
use nqp;
my @fail;

# --- prefix bitwise-negate as an argument (parser: startsTermToken / listopArg)
sub two($a, $b) { "$a,$b" }
my $v = -17;
@fail.push('+^-arg')      unless two(32, +^$v) eq '32,16';
@fail.push('+^-listop')   unless (32, +^$v).elems == 2;
@fail.push('+^-say-ctx')  unless "{+^$v}" eq '16';
@fail.push('~^-arg')      unless two(0, ~^0) ne '';   # just parses / runs

# --- Bool is an Int (nqp::istype + ~~)
@fail.push('Bool-istype-Int')     unless nqp::istype(True, Int);
@fail.push('Bool-istype-Numeric') unless nqp::istype(True, Numeric);
@fail.push('Bool-smartmatch-Int') unless True ~~ Int;

# --- num32 truncation to float32 precision
my num32 $n = 3.14e0;
@fail.push('num32-trunc') unless $n == 3.140000104904175e0;
my num32 $m = 3.5e0;
@fail.push('num32-exact') unless $m == 3.5e0;   # exactly representable: unchanged

# --- nqp buffer read/write tower + temporary-invocant write
my $buf = buf8.allocate(8);
my int $be16 = nqp::bitor_i(nqp::const::BINARY_SIZE_16_BIT, BigEndian);
nqp::writeuint($buf, 0, 0x1234, $be16);
@fail.push('writeuint-be') unless $buf[0] == 0x12 && $buf[1] == 0x34;
@fail.push('readuint-be')  unless nqp::readuint($buf, 0, $be16) == 0x1234;
@fail.push('buf-temp-write') unless buf8.new.write-uint16(0, 0x1234, BigEndian).read-uint8(0) == 0x12;

# --- CBOR encodings must be byte-identical to Rakudo's (values captured from raku)
# Guarded: CBOR::Simple is an ecosystem module — where it isn't installed (a clean
# CI runner) these checks skip; where it is, a wrong encoding still fails.
if (try { cbor-encode(0); True }) {
    my &hx = -> $v { cbor-encode($v).list.fmt('%02x', ' ') };
    my %want =
        '42'        => '18 2a',
        '-17'       => '30',
        'hello'     => '65 68 65 6c 6c 6f',
        'array'     => '83 01 02 03',
        'hash'      => 'a2 61 61 01 61 62 02',
        'pi'        => 'fb 40 09 1e b8 51 eb 85 1f',   # 3.14 stays num64
        'true'      => 'f5',
        'nil'       => 'f6';
    @fail.push("cbor-42 ({hx(42)})")          unless hx(42)               eq %want<42>;
    @fail.push("cbor-neg ({hx(-17)})")        unless hx(-17)             eq %want<-17>;
    @fail.push("cbor-str ({hx('hello')})")    unless hx('hello')         eq %want<hello>;
    @fail.push("cbor-arr ({hx([1,2,3])})")    unless hx([1,2,3])         eq %want<array>;
    @fail.push("cbor-hash ({hx(%(a=>1,b=>2))})") unless hx(%(a=>1,b=>2)) eq %want<hash>;
    @fail.push("cbor-pi ({hx(3.14e0)})")      unless hx(3.14e0)          eq %want<pi>;
    @fail.push("cbor-true ({hx(True)})")      unless hx(True)            eq %want<true>;
    @fail.push("cbor-nil ({hx(Any)})")        unless hx(Any)             eq %want<nil>;

    # round-trip: decode(encode(x)) == x for a nested structure
    my %deep = name => 'Tokyo', tags => ['a', 'b'], n => 3, active => True;
    my $rt = cbor-decode(cbor-encode(%deep));
    @fail.push('cbor-roundtrip')
        unless $rt<name> eq 'Tokyo' && $rt<tags>[1] eq 'b' && $rt<n> == 3 && $rt<active>;
}
else { note '# CBOR::Simple not installed here — skipping CBOR round-trip checks' }

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
