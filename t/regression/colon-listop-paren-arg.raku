# `@q.push: ($a, $b, $c, $d)` passes ONE argument — a four-element List — the way
# `@q.push(($a, $b, $c, $d))` does. The colon-listop form splatted a top-level
# comma list into the positional args, which is right for `push: 1, 2` and for
# `push: (1,2), (3,4)` (the OUTER comma separates arguments there) but wrong for a
# single parenthesised group: the parens make it one term.
#
# Path::Finder's directory walk does `@queue.prepend: @next` after
# `@next.push: ($item, $depth, $base, $result)`; splatting made its work queue
# four times too long and handed the next iteration an Int where it wanted an
# IO::Path (issue #34).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my @r;
@r = (); @r.push: (1, 2);           check @r.elems, 1, 'push: (1,2) pushes ONE list';
@r = (); @r.push: (1, 2);           check @r[0].List, (1, 2), '…and it is that list';
@r = (); @r.push(  (1, 2)  );       check @r.elems, 1, 'the parenthesised call form agrees';
@r = (); @r.unshift: (1, 2);        check @r.elems, 1, 'unshift: (1,2) too';
@r = (); @r.push: ('i', 0, 'b', Bool);
check @r[0].List, ('i', 0, 'b', Bool), 'the four-element shape Path::Finder queues';

# …and the forms that were already right, which must stay right
@r = (); @r.push: 1, 2;             check @r.elems, 2, 'push: 1, 2 is still two arguments';
@r = (); @r.push: (1, 2), (3, 4);   check @r.elems, 2, 'two groups are still two arguments';
@r = (); @r.append: (1, 2);         check @r.elems, 2, 'append still flattens its list';
@r = (); @r.push: [1, 2];           check @r.elems, 1, 'push: [1,2] pushes one Array';

# a colon-arg that is a single expression, not a comma list, is unaffected
my @z = 3, 1, 2;
check (@z.sort: -*.self).List,      (3, 2, 1), 'sort: -*.self (one expression)';
check (1, 2 Z+ 10, 20).List,        (11, 22),  'a Z-list colon arg still parses as one';
my @w = <b a>;
check (@w.sort: { $^x leg $^y }).List, ('a', 'b'), 'a block colon arg still parses';

# a NON-flattening receiver sees exactly one argument either way
class K { method one($a) { $a.elems } }
check K.one(  ('x', 'y', 'z')  ), 3, 'a parenthesised arg binds as one List';
check (K.new.one: ('x', 'y', 'z')), 3, '…and the colon form agrees';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
