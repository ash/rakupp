# CONTRACT test: `cas` is atomic on ALL FOUR container kinds, not just a plain
# lexical — a scalar, an array element, a hash element and an object attribute.
# S17-lowlevel/cas.t exercises exactly these, and three of them were broken.
#
# The shape: one worker swaps a shared slot between two objects with `cas` while
# another COPIES that slot the ordinary way. rakupp runs `start` blocks in real
# parallel and a Value is a fat struct, so the copy-out has to happen under the
# same striped lock the store takes (the P3 torn-copy contract). That contract
# covered a plain lexical and nothing else, so the other three tore: the reader
# took half an overwritten pointer and addref'd a control block that was already
# gone. Every run of this program segfaulted or aborted before the fix, on the
# array, hash and attribute legs; the lexical leg always survived.
#
# What is asserted is only what the contract promises: every copy is a WHOLE
# value — one of the two objects, never a mixture, and never a crash. WHICH of
# the two a given read sees is undefined and not checked.
#
# Unlike the ub-* family this stages NO deliberate race, so it runs STRICT under
# TSan, and two things keep it that way. Each worker's per-iteration `my` lives
# in its own ROUTINE frame, which is why every leg is a sub or a method rather
# than a bare block: a `my` inside a start block's loop lands in a shared Env,
# which is ub-env-sharing's question and not this one. And every name this file
# uses is declared BEFORE the first `start`, because defining one afterwards
# races the workers already reaching through that Env — the await-handshake
# class promise-chain owns. The only contended memory left is the slot itself.
my $N = (@*ARGS[0] // 8000).Int;

my class Node { has $.tag; }
my $a = Node.new(tag => 'a');
my $b = Node.new(tag => 'b');
sub other($v) { $v === $a ?? $b !! $a }
sub whole($v) { $v.defined && ($v === $a || $v === $b) }

my $lex = $a;                       # 1. plain lexical (already protected: the control leg)
my @arr; @arr[0] = $a;              # 2. array element
my @shp[1]; @shp[0] = $a;           # 3. SHAPED array element (cas.t uses `my @node-heads[1]`)
my %hsh; %hsh<k> = $a;              # 4. hash element
my class Holder {                   # 5. object attribute
    has $.slot;
    method swap($n)  { for ^$n { my $o = $!slot; cas($!slot, $o, other($o)) } }
    method watch($n) { my $c = 0; for ^$n { $c++ unless whole($!slot) }; $c }
}
my $obj = Holder.new(slot => $a);

sub lex-swap($n)  { for ^$n { my $o = $lex;    cas($lex,    $o, other($o)) } }
sub arr-swap($n)  { for ^$n { my $o = @arr[0]; cas(@arr[0], $o, other($o)) } }
sub shp-swap($n)  { for ^$n { my $o = @shp[0]; cas(@shp[0], $o, other($o)) } }
sub hsh-swap($n)  { for ^$n { my $o = %hsh<k>; cas(%hsh<k>, $o, other($o)) } }
sub lex-watch($n) { my $c = 0; for ^$n { $c++ unless whole($lex)    }; $c }
sub arr-watch($n) { my $c = 0; for ^$n { $c++ unless whole(@arr[0]) }; $c }
sub shp-watch($n) { my $c = 0; for ^$n { $c++ unless whole(@shp[0]) }; $c }
sub hsh-watch($n) { my $c = 0; for ^$n { $c++ unless whole(%hsh<k>) }; $c }

my $bad = 0;
my @w;

@w = (start { lex-swap($N) }), (start { lex-watch($N) });
await @w[0]; $bad += await @w[1];
@w = (start { arr-swap($N) }), (start { arr-watch($N) });
await @w[0]; $bad += await @w[1];
@w = (start { shp-swap($N) }), (start { shp-watch($N) });
await @w[0]; $bad += await @w[1];
@w = (start { hsh-swap($N) }), (start { hsh-watch($N) });
await @w[0]; $bad += await @w[1];
@w = (start { $obj.swap($N) }), (start { $obj.watch($N) });
await @w[0]; $bad += await @w[1];

die "torn or missing reads: $bad" if $bad;

# The compare is by IDENTITY: two DISTINCT objects that are structurally equal
# must not swap. `eqv` said they were the same and the swap went through.
my $x = Node.new(tag => 'a');
my $twin = Node.new(tag => 'a');       # eqv to $x, not identical to it
my $one = $x;
my $seen = cas($one, $twin, $b);
die 'cas swapped on a structurally-equal but distinct object' unless $one === $x;
die 'cas returned the wrong seen value' unless $seen === $x;

say "survived: $N swaps against $N reads on each of five container kinds";
say 'PASS';
