# Regression: the List/Array/Seq/Slip semantics sheet
# (docs/dev/findings/semantics/List-Array.md), implemented 2026-09-19.
#
# Every expectation below is the Rakudo 2026.08 answer, taken by running the
# sheet's own probes through the Homebrew binary — the sheet's recorded output
# where the two agree, and the binary's where the sheet had a transcription
# slip (LA-16 and LA-22 each lost a `.Seq`; LA-24 recorded X::Cannot::Lazy for
# a splice on a lazy array that the binary accepts).
#
# The sheet items each block covers are named in its heading.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }
sub is-raku($got, $want, $what) { ok($got eq $want, "$what (got $got, want $want)") }

# --- LA-33/34/35/32/29: the list builders answer a Seq -------------------------
is-raku(zip((1, 2), (3, 4)).raku,            '((1, 3), (2, 4)).Seq', 'zip is a Seq');
is-raku(roundrobin((1, 2), (3,)).raku,       '((1, 3), (2,)).Seq',   'roundrobin is a Seq');
is-raku(((1, 2) X (3, 4) X (5,)).raku,
        '((1, 3, 5), (1, 4, 5), (2, 3, 5), (2, 4, 5)).Seq',          'a chained X is a Seq');
is-raku(([Z] ((1, 2), (3, 4))).raku,         '((1, 3), (2, 4)).Seq', '[Z] is a Seq');
is-raku(flat(1, (2, (3,))).raku,             '(1, 2, 3).Seq',        'flat is a Seq');
is-raku((1, 2, 3).rotate.raku,               '(2, 3, 1).Seq',        '.rotate is a Seq');
is-raku(rotate((1, 2, 3), 1).raku,           '(2, 3, 1).Seq',        'the rotate sub too');
is-raku((1 xx Inf).^name,                    'Seq',                  'xx Inf is a Seq');
is-raku((3, 1, 2).sort(:k).raku,             '(1, 2, 0)',            '…but sort(:k) is a List');
is-raku(infix:<X>((1, 2), (3, 4), :with(&[+])).raku,
        '(4, 5, 5, 6).Seq',                          'infix:<X>(:with) folds each tuple');
is-raku((try reverse()) // $!.^name, 'X::NoZeroArgMeaning', 'reverse() has no zero-arg meaning');
is-raku(reverse((1, 2), 3).raku, '(3, $(1, 2)).Seq', 'reverse keeps several args as elements');
is-raku(((1..2).Seq xx 2).raku, '((1, 2), (1, 2)).Seq', 'xx caches a Seq');

# --- LA-34: where flat stops ---------------------------------------------------
is-raku([1, (2, 3)].flat.raku, '(1, $(2, 3)).Seq', "an ARRAY's element stays whole");
is-raku([1, [2, 3]].flat.raku, '(1, $[2, 3]).Seq', '…an inner Array too');
is-raku((1, [2, 3]).flat.raku, '(1, 2, 3).Seq',    "a LIST's element spreads");
is-raku([1, [2, 3]].flat(:hammer).raku, '(1, 2, 3).Seq', ':hammer flattens regardless');

# --- LA-21/16/23: typed arrays and their holes ---------------------------------
is-raku(Array[Int].new(1, 2).raku, 'Array[Int].new(1, 2)', '.raku names the element type');
is-raku((my Int @t1).raku,         'Array[Int].new()',     '…on an empty one too');
is-raku((my Int @t2).^name,        'Array[Int]',           '.^name is parameterized');
is-raku((my Int @t3).default.raku, 'Int',                  '…and .default is the type object');
is-raku((my int @t4 = 1, 2).raku,  'array[int].new(1, 2)', 'a native array uses its own ctor');
is-raku((try Array[Int].new("a")) // $!.^name,
        'X::TypeCheck::Assignment', 'the ctor type-checks its elements');
{
    my Int @a is default(0);
    @a[1] = 5;
    is-raku(@a.raku, 'Array[Int].new(0, 5)', 'an `is default` hole reads as its default');
    is-raku(@a[0].raku, '0',                 '…and so does a plain read');
    is-raku(@a.join(","), '0,5',             '…and a join');
}
{
    my @a is default(7);
    @a[2] = 1;
    is-raku(@a.raku, '[7, 7, 1]', 'an untyped `is default` array too');
}
{
    my Int @a is default(0) = 1, 2;
    @a[4] = 4; @a[4]:delete; @a[3]:delete;
    is-raku(@a.raku ~ " " ~ @a.pop.raku, 'Array[Int].new(1, 2) 2',
            'trailing deletes still shrink a defaulted array');
}
{
    my @a is default(9) = 1, 2;
    @a[0]:delete;
    is-raku(@a.shift ~ "", '9', 'shift reads a hole as the default');
}
is-raku((my @named).name, '@named', '.name is the variable, not "element"');

# --- LA-20: shaped arrays ------------------------------------------------------
{
    my @a[2]; @a[0] = 1;
    is-raku(@a.raku, 'Array.new(:shape(2,), [1, Any])', 'a 1-D shaped array names its shape');
    is-raku(((try { @a[2] = 1; "no" }) // $!.^name), 'X::AdHoc', '…and its bound is fixed');
}
{
    my @a[2;2];
    is-raku(((try { @a[2;0] = 1; "no" }) // $!.^name), 'X::AdHoc', 'so is a 2-D one');
}
is-raku(Array.new(:shape(2)).raku, 'Array.new(:shape(2,), [Any, Any])', 'Array.new(:shape)');
is-raku((try Array.new(:shape(1..2))) // $!.^name, 'X::AdHoc', 'a Range is not a shape');
{
    my int @a[2;2] = (42, 43), (44, 45);
    is-raku(@a.raku, 'array[int].new(:shape(2, 2), [42, 43], [44, 45])',
            'the rows of a shaped array print plain');
}

# --- LA-24/25: splice validates, grab consumes ---------------------------------
{
    my @a = 1..3;
    is-raku(((try @a.splice(9)) // ($!.^name ~ ":" ~ $!.what ~ ":" ~ $!.range)),
            'X::OutOfRange:Offset argument to splice:0..3', 'splice checks its offset');
}
{
    my @a = 1..3;
    is-raku(((try @a.splice(1, -1)) // ($!.^name ~ ":" ~ $!.what ~ ":" ~ $!.range)),
            'X::OutOfRange:Size argument to splice:0..^2', '…and its size');
}
is-raku((try [].splice(1)) // $!.^name, 'X::OutOfRange', 'an empty array has no offset 1');
{
    my @a = 1..3;
    is-raku((try @a.splice(0, 1, 1..*)) // $!.^name, 'X::Cannot::Lazy',
            'a lazy replacement is refused');
}
{
    my Int @b = 1, 2, 3;
    is-raku(@b.splice(1, 1).of.raku, 'Int', 'the removed array keeps the element type');
}
{
    my @a = 1, 2, 3;
    my $one = @a.grab;
    ok($one ~~ Int && @a.elems == 2, 'grab removes one element');
    is-raku(@a.grab(*).elems ~ " " ~ @a.elems, '2 0', 'grab(*) takes the rest');
    is-raku([].grab.raku, 'Nil', 'grab on an empty array is Nil');
    is-raku([].grab(2).raku, '().Seq', '…and the counted form an empty Seq');
}

# --- LA-27/29/23/10/14/02: what a lazy list allows -----------------------------
is-raku((1..*).list.elems.^name,   'Failure', 'elems on a lazy list is a Failure');
is-raku((1..*).list.reverse.^name, 'Failure', '…and so is reverse');
is-raku((1..*).list.sum.^name,     'Failure', '…sum');
is-raku((1..*).list.pick.^name,    'Failure', '…pick');
is-raku((1..*).list.Capture.^name, 'Failure', '…and Capture');
is-raku((try (1..*).list.sort.eager) // $!.^name, 'X::Cannot::Lazy', 'sort throws instead');
is-raku((try (1..*).list.tail) // $!.^name,       'X::Cannot::Lazy', '…and tail');
is-raku((1..*).list.Bool.raku, 'Bool::True', 'a lazy list is True');
is-raku((1..*).list.join(",").raku, '"..."', 'join marks what it has not pulled');
{
    my @a = 1..*; @a[2];
    is-raku(@a.join(","), '1,2,3,...', '…and shows the prefix it has');
    is-raku(@a.Str.raku, '"1 2 3 ..."', 'Str likewise');
    is-raku(@a.gist, '[...]', 'a lazy ARRAY gists as [...]');
    is-raku(@a.raku, '[...]', '…and rakus the same');
}
is-raku((1..*).list.raku.substr(*-8), '..).lazy', 'a lazy LIST ends .lazy, not .lazy.Seq');
is-raku((1..*).map({ $_ }).Str.raku, '"..."', 'an unpulled lazy Seq stringifies to ...');
{
    my @a; is-raku(((try { @a.append(1..*); "no" }) // $!.^name), 'X::Cannot::Lazy',
                   'append refuses a lazy source');
}
{
    my @a = 1..*;
    is-raku(((try { @a.push(1); "no" }) // $!.^name), 'X::Cannot::Lazy',
            'and a lazy array refuses push');
    is-raku(@a.pop.^name, 'Failure', '…while pop is a Failure');
}
is-raku(Array.new(1..*).is-lazy.raku, 'Bool::True', 'Array.new keeps an endless source lazy');
is-raku((1..*).list.head(3).eager.^name, 'List', '.eager answers a List');

# --- LA-04/22: the single-argument rule ----------------------------------------
is-raku(Array.new((1, 2), (3, 4)).raku, '[(1, 2), (3, 4)]', 'several args are elements');
is-raku(Array.new((1, 2)).raku,         '[1, 2]',           'one Iterable is the list');
is-raku(Array.new($(1, 2)).raku,        '[(1, 2),]',        'an itemized one is an element');
is-raku(cache(1, 2).^name,              'Array',            'the cache sub answers an Array');
is-raku((my @s1 = set(1)).raku,         '[1 => Bool::True]', '@a = set(1) is an Array of pairs');
is-raku((my @s2 = {a => 1}).raku,       '[:a(1)]',           '…and so is @a = {…}');
{
    my @a; @a.append(%(a => 1, b => 2));
    is-raku(@a.elems.raku, '2', 'append spreads a Hash into its pairs');
}

# --- LA-18: slice assignment ---------------------------------------------------
{ my @a = 1, 2, 3; @a[*] = 0;          is-raku(@a.raku, '[0, Any, Any]', '@a[*] = v is a slice'); }
{ my @a = 1, 2, 3; @a[*] = 7, 8, 9, 10; is-raku(@a.raku, '[7, 8, 9]',    '…extra values drop'); }
{ my @a = 1, 2, 3; @a[*-1, 0] = 0, 9;   is-raku(@a.raku, '[9, 2, 0]',    'a *-1 index resolves'); }
{ my @a = 1, 2, 3; @a[] = 5, 6;         is-raku(@a.raku, '[5, 6]',       'the zen slice replaces'); }

# --- LA-06: Empty is a singleton ------------------------------------------------
ok(Empty === Empty,    'Empty is one object');
ok(slip() === Empty,   'slip() is that object');
is-raku(Empty.map({ $_ }).raku, 'Empty', 'a list method on Empty short-circuits');
is-raku(Empty.List.raku,        '()',    '…but .List is the empty list');

# --- LA-08/15/17/19/36/12/31: the rest -----------------------------------------
{ my @a = 1, 2; @a.map(* *= 2); is-raku(@a.raku, '[2, 4]', 'map(* *= 2) writes through'); }
{ my @a = 1, 2, 3; @a.first = 9; is-raku(@a.raku, '[9, 2, 3]', '.first is an lvalue'); }
{ my @a = 1, 2, 3; @a.first(* %% 2).++; is-raku(@a.raku, '[1, 3, 3]', '…with a matcher too'); }
{
    my $i = -1; my $f = (1, 2)[$i];
    is-raku($f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ $f.exception.range,
            'Failure:X::OutOfRange:0..^Inf', 'a negative index carries its range');
    my @a = 1, 2;
    is-raku((@a[$i]:delete).^name, 'Failure', '…and so does a negative delete');
    is-raku((try @a[Int]) // $!.^name, 'X::AdHoc', 'a type object is not an index');
}
{ my @a = <a b c>; is-raku((try @a[0]:foo) // $!.^name, 'X::Multi::NoMatch', 'unknown adverb'); }
{ my @a = 1, 2; is-raku(@a[0;0].^name, 'Int', 'a scalar element indexes as a one-item list'); }
is-raku([1, 2].WHICH.^name, 'ObjAt',      'an Array identifies by object');
is-raku(1.WHICH.^name,      'ValueObjAt', '…and an Int by value');
is-raku((1, (2, 3)).fmt("<%s>", ",").raku, '"<1>,<2>,<3>"', 'fmt recurses into a nested list');
is-raku((try (1, 2).fmt("%d and %d")) // $!.^name, 'X::AdHoc', 'fmt formats one element at a time');
{
    my ($a, $b, $c) = (1, 2), 3;
    is-raku(($a, $b, $c).raku, '($(1, 2), 3, Any)', 'an inner list is ONE destructured value');
}
{
    my ($a, %h) = "x", {:k<v>};
    is-raku(%h<k>, 'v', '…while a % target still takes the pairs');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
