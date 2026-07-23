# Regression: batch 11b typed-blob cluster (Digest::SHA1 pipeline). Contract:
# exit 0 + last line PASS.
my @fail;

# 1. element-width typed blobs: blob32 stores 32-bit LE words
my $b = blob32.new(0x11223344, 5);
@fail.push('blob32-elems') unless $b.elems == 2;
@fail.push('blob32-index') unless $b[0] == 0x11223344;
@fail.push('blob32-numify') unless $b == 2;           # .Int = element count
@fail.push('blob32-list') unless $b.list eqv (0x11223344, 5);
@fail.push('blob8-bytes') unless "abc".encode.elems == 3 && (8 * "abc".encode) == 24;

# 2. @$blob and coerce to a native-int array
my uint32 @W = $b;
@fail.push('coerce-array') unless @W[0] == 0x11223344;
@fail.push('at-slip') unless (my @x = @$b) eqv (0x11223344, 5);

# 3. radix digit-list
@fail.push('radix-list') unless :256[97, 98, 99, 128] == 0x61626380;
@fail.push('radix-slip') unless ((97,98),(0,0)).map({ :256[|@^a] }).List eqv (0x6162, 0);

# 4. colon-arg with a Z that's looser than comma
my $z = blob32.new: blob32.new(1,2) Z+ blob32.new(10,20);
@fail.push('colon-Z') unless $z.list eqv (11, 22);

# 5. ( expr; ) is the grouped value, not a 1-list
@fail.push('semi-paren') unless (5;) === 5;
@fail.push('semi-list') unless (1,2;3,4).elems == 2;   # a real semicolon list still lists

# 6. native uint32 push wraparound
my uint32 @m;
@m.push(0xFFFFFFFF + 5);
@fail.push('uint32-wrap') unless @m[0] == 4;

# 7. the payoff: byte-correct SHA1 if the module is available
if try ::('Digest::SHA1::&sha1') {
    my $d = ::('Digest::SHA1::&sha1')("abc");
    @fail.push('sha1-value') unless $d.list.map(*.fmt("%02x")).join
        eq 'a9993e364706816aba3e25717850c26c9cd0d89d';
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
