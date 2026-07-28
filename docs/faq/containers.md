# FAQ — containers, itemisation, and why my list has one element

Every snippet on this page was run on both Raku++ and Rakudo and produces
identical output.

This is the single most confusing corner of Raku, and it is not a Raku++ quirk —
Rakudo behaves the same way. It is worth understanding once.

## Why is `my @a = %h<k>` one element?

```raku
my %h;
my @x = 7, 8, 9;
%h<k> = @x;

my @a = %h<k>;
say @a.elems;      # 1   ← not 3
```

Because a hash *value* lives in a Scalar container. Anything read out of one is
**itemised**: it behaves as a single thing in list context, however many elements
it contains. The same is true of a scalar variable:

```raku
my @x = 7, 8, 9;
my $s = @x;
my @b = $s;
say @b.elems;      # 1
```

`@x` itself is not itemised, so it spreads:

```raku
my @x = 7, 8, 9;
my @c = @x;
say @c.elems;      # 3
```

## How do I get the elements out?

Any of these — they all decontainerise:

```raku
my %h; %h<k> = [7, 8, 9];
my @a = @(%h<k>);        # the @ contextualiser
my @b = %h<k>.list;
my @c = %h<k>.Array;
my @d = |%h<k>;          # flattening prefix
say @a.elems, @b.elems, @c.elems, @d.elems;   # 3333
```

`@(…)` is the usual one. Reach for `.Array` when you want a real `Array` back
(a mutable copy), `.list` when a `List` is fine.

## What makes something itemised?

The `$` sigil, in all its forms:

```raku
say (1, 2, 3).elems;            # 3
say $(1, 2, 3).elems;           # 1   — $() itemises
my @flat = (1, 2), (3, 4);
say @flat.elems;                # 2   — each inner list is one element
say [1, 2].elems;               # 2   — [] is an Array, not itemised
```

And it survives into `.raku`, which is how you can see it:

```raku
my %h; %h<k> = [1, 2];
say %h<k>.raku;                 # $[1, 2]   ← the $ marks the itemisation
say @(%h<k>).raku;              # [1, 2]
```

## Passing a list to a routine

An itemised value arrives as **one** argument:

```raku
sub count(*@a) { @a.elems }
my $items = (1, 2, 3);
say count($items);              # 1
say count(|$items);             # 3   — | flattens it into the argument list
say count(@($items));           # 3
```

This is the usual cause of "my sub got one argument and I passed three things".

## Attributes follow their sigil

An attribute's sigil is a container type, so the value is coerced to match:

```raku
class T {
    has @.list = (1, 2);        # a List initialiser…
    has %.h    = (a => 1);
}
say T.new.list.WHAT;            # (Array)   ← …but the attribute is an Array
say T.new.h.WHAT;               # (Hash)
```

That matters when you render one — an `Array` reprs as `[1, 2]` where a `List`
would repr as `(1, 2)`.

## Gotchas

**`.elems` and binding can disagree if a coercion is wrong.** `$v.Array.elems`
must equal `(my @a = $v.Array).elems` — if they differ, that is a bug, not a
subtlety. (Raku++ had exactly that bug before v1.2.5: `.Array` reported 3 and
bound 1.)

**A `for` loop binding itemises.** Iterating pairs of values binds each into a
scalar, so a nested list stays whole:

```raku
for [1, 2], [3, 4] -> $pair { say $pair.elems }    # 2, then 2
```

**`.flat` stops at an itemised value.** That is what it is for — itemisation is
the mechanism that lets a structure survive flattening.

---

Back to the [FAQ index](README.md).
