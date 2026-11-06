# A Hash handed to a flattening slurpy is its Pairs (2026-11-06), as Rakudo
# 2026.09 binds it: `sub f(*@a) {…}; f(%h)` binds the two Pairs of a
# two-entry hash, not one Hash. rakupp's slurpySpread dissolved Arrays, Lists
# and Ranges but passed a Hash or a Map whole, and so did the single-argument
# rule of `+@a`; a `where` on such a slurpy saw the same wrong list, so a
# multi picked the wrong candidate. Found by showcase/ledger's generator,
# which built its postings as hashes and got different journals from the two
# engines.
#
# The implicit `@_` is a `*@_` and now binds the same way — it spread only a
# List, so `f(@x)` handed it one Array and `f(1..3)` one Range. A Set or a Bag
# stays whole (Rakudo does not flatten them), and so does anything in a `$`
# container. (A lazy argument makes `@_` lazy, as it does `*@a`: that is
# checked in exe-sub-reads-at-underscore.raku, which the JS corpus skips.)
#
# The same file runs in t/js's corpus, where the interpreter is the oracle;
# exe-slurpy-flattens.raku checks a compiled `--exe` program. Contract: exit
# 0 + last line PASS.

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
sub names(@a) { @a.map(*.^name).join(',') }

sub flat(*@a)   { names(@a) }
sub one(+@a)    { names(@a) }
sub keep(**@a)  { names(@a) }
sub under       { names(@_) }

my %h = a => 1, b => 2;
my %one = k => 1;
my $item = %h;
my @x = 1, 2, 3;
my %empty;

check flat(%h),            'Pair,Pair',           '*@ takes a Hash as its Pairs';
check flat(%h, %h),        'Pair,Pair,Pair,Pair', '…each of several';
check flat($item),         'Hash',                '…but not one in a $ container';
check flat($%h),           'Hash',                '…or itemized with $( )';
check flat({ x => 1 }),    'Pair',                '…a hash literal too';
check flat((%h, 1)),       'Pair,Pair,Int',       '…and one inside a List';
check flat([%h, 1]),       'Hash,Int',            '…but not one inside an Array';
check flat(%h.Map),        'Pair,Pair',           '*@ takes a Map as its Pairs';
check flat(set <p q>),     'Set',                 'a Set stays whole';
check flat(bag <p q p>),   'Bag',                 'a Bag stays whole';
check flat(1, %empty, 2),  'Int,Int',             'an empty Hash adds nothing';
sub raw(*@a) { @a }
check raw(%one).head.key ~ raw(%one).head.value, 'k1', 'the Pair is the entry';

check one(%h),             'Pair,Pair',           '+@ takes a lone Hash as its Pairs';
check one(%h, %h),         'Hash,Hash',           '…but several as they are';
check one(set <p>),        'Set',                 '…and a lone Set whole';
check keep(%h, @x),        'Hash,Array',          '**@ keeps everything';

check under(%h),           'Pair,Pair',           '@_ takes a Hash as its Pairs';
check under(@x),           'Int,Int,Int',         '@_ takes an Array as its elements';
check under((1, (2, 3))),  'Int,Int,Int',         '@_ flattens through Lists';
check under(1..3),         'Int,Int,Int',         '@_ takes a Range as its elements';
check under($item),        'Hash',                '@_ leaves an itemized Hash whole';

multi m(*@a where *.elems == 2) { 'two' }
multi m(*@a)                    { 'other' }
check m(%h),               'two',                 'a where on *@ sees the Pairs';
check m(%h, 1),            'other',               '…all three of them';
multi n(+@a where *.elems == 2) { 'two' }
multi n(+@a)                    { 'other' }
check n(%h),               'two',                 'a where on +@ sees the Pairs';

check Array.new(%one).raku,          '[:k(1)]',              'Array.new(%h) is its Pairs';
check Array.new(%one, 2).raku,       '[{:k(1)}, 2]',         'Array.new(%h, 2) keeps the Hash';
check Array.new((1, 2), (3, 4)).raku, '[(1, 2), (3, 4)]',    'Array.new of two Lists is two elements';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
