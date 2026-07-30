# Regression: the last two layers of the Digest::MD5 thread — after these,
# md5() is byte-correct against the RFC 1321 vectors.
#
# 1. `for` over a Blob/Buf iterated the BUFFER as one item; Raku iterates its
#    ELEMENTS unless the value sits in a scalar container. The rule is plain
#    itemization, established against Rakudo:
#      for blob32.new(1,2,3,4) { }   → 4 iterations (not itemized)
#      my $b = …; for $b { }         → 1 iteration  (scalar container)
#    Digest::MD5 emits its digest with `$buf.write-uint32(…) for <result>`,
#    so a 4-element blob32 wrote ONE word: a 4-byte digest instead of 16.
#    (Both `for` shapes needed it — the block form and the statement
#    modifier, which is the one MD5 uses.)
# 2. `(my $x .= new)[$i] = v` — a parenthesized MUTATING method call as an
#    assignment target — threw "Target is not assignable". `.=` is a
#    MethodCall node, not an Assign, so the lvalue path had no arm for it
#    (the parenthesized `=` form was already handled). Digest::SHA2 keeps its
#    message schedule in `(state buf32 $w .= new)[$j] = …`.
# 3. parse-base as a SUB (`parse-base($str, 16)`) — only the method existed;
#    Digest's own md5.t builds its expected digests with the sub form.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. Blob iteration follows itemization
my $n = 0;
$n++ for blob32.new(1, 2, 3, 4);
@fail.push("modifier for: $n") unless $n == 4;
my $m = 0;
for Blob.new(1, 2, 3) { $m++ }
@fail.push("block for: $m") unless $m == 3;
my $held = blob32.new(1, 2, 3, 4);
my $k = 0;
$k++ for $held;                       # scalar container → ONE item
@fail.push("itemized: $k") unless $k == 1;
my @topics;
@topics.push($_) for blob32.new(5, 6);
@fail.push("topics: {@topics}") unless @topics eqv [5, 6];

# 2. mutating-call lvalue — the assignment REACHES the declared container
#    (NOT asserted: what a Buf does with an index-assign past its end. rakupp
#    currently converts the buffer to an Array and leaves Any holes where
#    Rakudo zero-fills and stays a Buf — a separate gap, tracked, and the
#    reason Digest::SHA2 computes a wrong-but-well-formed digest.)
my $j = 1;
(my buf32 $w .= new)[$j] = 9;
@fail.push("my .= index: {$w[1]}") unless $w[1] == 9;
sub sched($i) { (state buf32 $s .= new)[$i] = $i * 10; $s[$i] }
sched(0);
@fail.push("state .= index: {sched(1)}") unless sched(1) == 10;

# 3. parse-base sub form (and the method still works)
@fail.push('parse-base sub')    unless parse-base('ff', 16) == 255;
@fail.push('parse-base method') unless 'ff'.parse-base(16) == 255;
@fail.push('parse-base blob')
    unless Blob.new(parse-base('0cc1', 16).polymod(256 xx *).reverse).list eqv (12, 193);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
