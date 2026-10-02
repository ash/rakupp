# FAQ — parametric roles, and what role Foo[::T] means

Every snippet on this page was run on both Raku++ and Rakudo. They give
identical output except in one place, which is shown with both outputs.

```raku
role LinearArray[::T] { ... }
```

This is a **parametric role**: a role that takes arguments, much like a
generic in C++, Java or Rust. The square brackets are the role's parameter
list. They work like a routine's signature, but the arguments are bound when
the role is applied to a class, not when something is called.

## What is `::T`?

It is a **type capture**. Whatever type is passed in gets the name `T`, and
inside the role `T` can go anywhere a type name can go: on an attribute, on a
parameter, or as a type object you call methods on.

```raku
role LinearArray[::T] {
    has T @.items;
    method push(T $x) { @!items.push($x); self }
    method of-type    { T.^name }
}

class IntList does LinearArray[Int] { }

my $l = IntList.new;
$l.push(1).push(2);
say $l.items;       # [1 2]
say $l.of-type;     # Int
```

The type is enforced. `push` on an `IntList` takes an `Int`, so anything else
fails to bind:

```raku
role LinearArray[::T] {
    method push(T $x) { say "pushed $x" }
}
class IntList does LinearArray[Int] { }

IntList.new.push("three");
CATCH { default { say .^name } }    # X::TypeCheck::Binding::Parameter
```

Type captures are not special to roles. A sub can capture the type of one
argument and require it of another:

```raku
sub same-type(::T $a, T $b) { "both are {T.^name}" }
say same-type(1, 2);          # both are Int
say same-type('a', 'b');      # both are Str
say same-type(1, 'b');        # dies: X::TypeCheck::Binding::Parameter
```

## Do I need a class to use one?

No. Calling `.new` on a role **puns** it: Raku makes an anonymous class that
does the role and instantiates that. This works for a parametrised role too:

```raku
role LinearArray[::T] {
    has T @.items;
    method of-type { T.^name }
}
my $s = LinearArray[Str].new(items => <a b c>);
say $s.items;      # [a b c]
say $s.of-type;    # Str
```

## Several parameters, and defaults

The brackets take a full signature, so you can have more than one parameter,
and defaults work as they do in a sub:

```raku
role Pairing[::K, ::V = Str] {
    method kinds { K.^name ~ ' => ' ~ V.^name }
}
say Pairing[Int].new.kinds;         # Int => Str
say Pairing[Int, Num].new.kinds;    # Int => Num
```

## The parameters do not have to be types

Any value can be a parameter. Here the role is parametrised by a size:

```raku
role Ring[Int $size] {
    has @.slots = Any xx $size;
    method size { $size }
}
my $r = Ring[4].new;
say $r.size;          # 4
say $r.slots.elems;   # 4
```

## Can I overload a role on its parameters?

Yes. Declare the role more than once with different signatures, and Raku picks
the variant by multiple dispatch on the arguments, the same way it picks a
`multi` candidate:

```raku
role Describe[Int $n] { method what { "an integer, $n" } }
role Describe[Str $s] { method what { "a string, $s" } }

say Describe[42].new.what;      # an integer, 42
say Describe['hi'].new.what;    # a string, hi
```

## Checking what something does

Smartmatching against the bare role name matches any parametrisation. With
arguments, it matches only that one:

```raku
role LinearArray[::T] { }
class IntList does LinearArray[Int] { }

say IntList ~~ LinearArray;          # True
say IntList ~~ LinearArray[Int];     # True
say IntList ~~ LinearArray[Str];     # False
```

## Where you have already met them

Typed arrays and hashes are parametric types too. `my Int @a` declares an
`Array[Int]`, and you can name that type directly:

```raku
my Int @a = 1, 2, 3;
say @a.^name;               # Array[Int]
say @a ~~ Array[Int];       # True

my %h{Str} of Int;
say %h.^name;               # Hash[Int,Str]
say Array[Str].new(<x y>);  # [x y]
```

`Hash[Int,Str]` names the value type first and the key type second, the same
order as `of Int` and `{Str}` in the declaration.

## What if I leave the arguments off?

A role whose parameter has no default needs an argument. Rakudo rejects
`does LinearArray` at compile time:

```raku
role LinearArray[::T] { has T @.items }
class Plain does LinearArray { }
say Plain.new.items.^name;
# Rakudo:  ===SORRY!=== No appropriate parametric role variant available
#          for 'LinearArray'
# Raku++:  Array[T]
```

Raku++ accepts the program and leaves `T` unbound, which is not something to
rely on. If a role should work without an argument, give the parameter a
default, and then the two engines agree:

```raku
role LinearArray[::T = Any] { has T @.items }
class Plain does LinearArray { }
say Plain.new.items.^name;     # Array[Any]
```
