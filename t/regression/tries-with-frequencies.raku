# Regression: the six engine gaps behind `rakupp install
# ML::TriesWithFrequencies` (issue #53, 2026-08-31). The distribution's own
# suite is what `install` runs, and it failed at the first file with "No such
# method 'trieRootLabel'". Expectations read off RAKUDO first.
#
#   1.  `my $.x` in a class or role body — a lexical PLUS a package accessor of
#       the bare name, composed into the class and callable on the type object.
#   2.  a `&x` parameter accepts a Callable TYPE OBJECT (`WhateverCode` passed
#       where a post-processing function is optional).
#   3.  prefix `^` curries over a Whatever: `@a[^(*-1)]` is everything but the
#       last, not an empty range.
#   4.  `%h.push: ($k, $v)` pairs consecutive non-Pair items up as key/value.
#   5.  `.sort(-> $a, $b { … })` with a Bool-answering comparator orders the
#       list — Rakudo's merge takes the right element only when `by(l, r) > 0`.
#   6.  `.append($[…])` adds an ITEMIZED array as one element, and `».[0]` /
#       `».{$k}` subscript every element rather than the list itself.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# ---- 1. `my $.x` is a lexical and an accessor --------------------------------
{
    role Labels {
        my Str $.rootLabel = 'TRIEROOT';
        method label-from-role { $.rootLabel }
    }
    class Node does Labels {
        my @.dims = 2, 3;
        my %.opts = a => 1;
        method label-from-class { $.rootLabel }
    }
    check(Node.new.label-from-role,  'TRIEROOT', 'a role method reads its own `my $.x`');
    check(Node.new.label-from-class, 'TRIEROOT', 'so does the composing class');
    check(Node.new.rootLabel,        'TRIEROOT', 'the accessor is composed in');
    check(Node.rootLabel,            'TRIEROOT', '…and answers on the type object');
    check(Node.dims,                 [2, 3],     '@. and %. declare accessors too');
    check(Node.opts,                 {a => 1},   '');
    # the generated accessor hands back the CONTAINER, so it is writable
    Node.rootLabel = 'OTHER';
    check(Node.new.label-from-class, 'OTHER',    'the accessor is rw, and the storage is shared');
    Node.rootLabel = 'TRIEROOT';
    # a plain `my $x` beside it stays a separate variable
    class Twin { my $.n = 5; my $n = 7; method plain { $n } }
    check(Twin.n,        5, '`my $.n` and `my $n` are two variables');
    check(Twin.new.plain, 7, '');
}

# ---- 2. a `&x` param takes a Callable type object ----------------------------
{
    my @seen;
    multi sub step(&pre, &post, UInt $l) { @seen.push('three'); 'three' }
    multi sub step(&pre, UInt $l)        { @seen.push('two');   'two' }
    check(step(-> $x { $x }, WhateverCode, 1), 'three', 'WhateverCode binds a `&` param');
    check(step(-> $x { $x }, 1),               'two',   '…without stealing the shorter candidate');
    check(step(-> $x { $x }, Callable, 1),     'three', 'so does bare Callable');
    # a NON-Callable type object still refuses, which is what keeps two
    # candidates apart when only one of them takes a routine
    multi sub tag(Any $parent, &task) { 'parent+task' }
    multi sub tag(&task, *%data)      { 'task' }
    check(tag(-> {}, x => 1), 'task',        'a non-Callable first argument is not a `&` match');
    check(tag(42, -> {}),     'parent+task', '');
}

# ---- 3. prefix `^` curries over a Whatever -----------------------------------
{
    my @chars = <b a r>;
    check((^(*-1)).WHAT.^name, 'WhateverCode', '`^(*-1)` is a WhateverCode, not a Range');
    check(@chars[^(*-1)].List, ('b', 'a'),     'so the subscript drops the last element');
    check(@chars[^(*-1)].reverse.List, ('a', 'b'), '');
    check((^*).WHAT.^name,     'WhateverCode', '`^*` too');
    check((^(*-1))(5).List,    (0, 1, 2, 3),   'called, it ranges to the argument');
}

# ---- 4. Hash.push pairs bare items up ----------------------------------------
{
    my %h; %h.push: ('a', 42);
    check(%h, {a => 42}, '`%h.push: ($k, $v)` is one key/value pair');
    my %i; %i.push('a', 1, 'b', 2);
    check(%i, {a => 1, b => 2}, '…and a longer run alternates');
    my %j = a => 1; %j.push('a', 2);
    check(%j, {a => [1, 2]}, 'an existing key accumulates a list');
    my %k; %k.push(:x(1), 'y', 2);
    check(%k, {y => 2}, 'a NAMED argument is still a no-op');
}

# ---- 5. a Bool-answering sort comparator orders ------------------------------
{
    check((3, 1, 4, 1, 5).sort(-> $a, $b { $b > $a }).List, (5, 4, 3, 1, 1),
          'a Bool comparator sorts descending');
    check((3, 1, 4, 1, 5).sort({ $^b <=> $^a }).List, (5, 4, 3, 1, 1),
          '…and an Order-answering one is unchanged');
    check((3, 1, 4, 1, 5).sort({ $^a <=> $^b }).List, (1, 1, 3, 4, 5), '');
    # stable: equal-comparing elements keep their input order
    my @pairs = (b => 2, a => 2, c => 1, d => 3);
    check(@pairs.sort(-> $x, $y { $y.value > $x.value })>>.key.List, ('d', 'b', 'a', 'c'),
          'ties keep their input order');
}

# ---- 6. itemized append, and hyper subscripts --------------------------------
{
    my @paths;
    my @prefix = 7, 8;
    @paths.append($[|@prefix, 9]);
    @paths.append($[1]);
    check(@paths.elems, 2, 'an itemized array appends as ONE element');
    check(@paths[0].List, (7, 8, 9), '');
    my @flat; @flat.append([1, 2]);
    check(@flat.elems, 2, '…while a plain one still flattens');
    my @rows = ([1, 2], [3, 4]);
    check((@rows>>.[0]).List, (1, 3), '`».[0]` subscripts every element');
    check((@rows».[*-1]).List, (2, 4), '');
    my @maps = ({a => 1}, {a => 2});
    check((@maps>>.{'a'}).List, (1, 2), '`».{$k}` too');
    check((@maps>>.<a>).List, (1, 2), '…as `».<k>` already did');
}

say @fail ?? "FAILED:\n" ~ @fail.join("\n") !! 'PASS';
exit @fail ?? 1 !! 0;
