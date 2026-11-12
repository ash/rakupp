# Regression: values a program compiled with `--exe` computed differently from
# the interpreter, found by t/exe/parity.raku (compiled against interpreted,
# every program in t/regression and examples):
#
#   { 1 }.^name             a bare block was a Sub (a pointy one was a Block)
#   [ %$h ], [ %(…) ]       the one-argument rule did not spread a hash's pairs
#   %<x>, @<x>              a Match in hash/list context is its named/positional
#                           captures; `%<x>` died "Odd number of elements"
#   sub f(buf8 $b)          a lowercase type (buf8, blob8, utf8) was not checked
#   %h<k> = @x              an element is a Scalar container and holds the list
#                           as ONE item; `my @i = %h<k>` had three elements
#   %h<missing>, @a[99]     read Nil, not (Any); a Set's missing key is False and
#                           a Bag's 0, as a List's and a Map's are Nil
#   my $u; $u<k>, Str<k>    read Nil, not (Any)
#
# t/exe/parity.raku compiles this file and compares the binary with the
# interpreter. Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

# blocks
my $blk = { 1 };
my $tpc = { $_ + 1 };
my $plh = { $^a };
ck ($blk.^name, $tpc.^name, $plh.^name, (-> $x { $x }).^name, (sub { 2 }).^name),
   <Block Block Block Block Sub>, 'a bare block is a Block';

# the one-argument rule over a hash
my $h = { a => 1, b => 2 };
my @l = a => 1, b => 2;
ck [ %$h ].elems, 2, '[ %$h ] spreads';
ck [ %( a => 1, b => 2 ) ].elems, 2, '[ %( … ) ] spreads';
ck [ %(@l) ].elems, 2, '[ %(@l) ] spreads';
ck [ %$h ].sort(*.key).map(*.key).List, <a b>, '…into Pairs';
ck [ %() ].elems, 0, 'an empty one is empty';
ck [ $h ].elems, 1, 'an itemized hash is one element';

# a Match in hash and list context
'ab' ~~ / $<foe>=(a) (b) /;
ck (%<foe>).keys.List, (), '%<x> is the named captures of $<x>';
ck (@<foe>).elems, 0, '…and @<x> its positional ones';
ck %($/).keys.List, ('foe',), '%($/)';

# a lowercase parameter type
sub b8(buf8 $x) { 1 }
ck (try b8(blob8.new(1))) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'a blob8 for buf8';
ck b8(buf8.new(1)), 1, '…and a buf8 binds';

# one element holds a list as one item
my @x = 1, 2, 3;
my %e;
%e<k> = @x;
ck (my @i = %e<k>).elems, 1, '%h<k> = @x is one item';
my @a;
@a[0] = @x;
ck (my @j = @a[0]).elems, 1, '@a[0] = @x is one item';
my $k = 'z';
%e{$k} = (4, 5);
ck (my @m = %e{$k}).elems, 1, '%h{$k} = (…) is one item';
my @s;
@s[0, 1] = 7, 8;
ck @s, [7, 8], 'a slice store still spreads';

# missing elements
my %mh;
my @ma;
ck (%mh<x>.^name, @ma[3].^name), <Any Any>, 'a missing Hash key and Array element are (Any)';
my @hole;
@hole[3] = 1;
ck @hole[0].^name, 'Any', "an Array's hole is (Any)";
ck ((1, 2)[5].^name, Map.new((a => 1))<b>.^name, (1..3)[7].^name), <Nil Nil Nil>, 'a List, a Map, a Range: Nil';
ck (set(<a>)<z>, bag(<a>)<z>), (False, 0), "a Set's missing key is False, a Bag's 0";
ck (%mh<x>:exists), False, ':exists';
my $u;
ck ($u<k>.^name, $u[0].^name, Str<k>.^name), <Any Any Any>, 'an undefined base reads (Any)';
ck "{%mh<q>.gist}", '(Any)', 'and says so';

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
