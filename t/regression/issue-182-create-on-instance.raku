# Issue #182: `.CREATE` on a DEFINED invocant died "No such method 'CREATE'",
# for a user class and a built-in alike — only a type object had one. Rakudo's
# CREATE is Mu's: on an instance it allocates another of the invocant's type,
# so `self.CREATE` works in a method called on an instance. And on a BUILT-IN
# type object it built a nameless object: `Mu.CREATE` gisted as "Object", was
# not `.WHAT === Mu`, and `Int.CREATE` / `Hash.CREATE` were no Int or Hash.
# A class's own `method CREATE` was also skipped on its type object
# (`P.CREATE` answered a fresh P instead of calling the method).
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# the issue
my $obj = Mu.CREATE;
check $obj.gist, 'Mu.new', 'Mu.CREATE gists as Mu.new';
check $obj.^name, 'Mu', 'Mu.CREATE is a Mu';
check $obj.WHAT === Mu, True, 'Mu.CREATE.WHAT is Mu';
check $obj.defined, True, 'Mu.CREATE is defined';
my $obj2 = $obj.CREATE;
check $obj2.gist, 'Mu.new', 'CREATE on a Mu instance';
check Mu.CREATE.CREATE.CREATE.^name, 'Mu', 'chained CREATE';
check Any.CREATE.gist, 'Any.new', 'Any.CREATE';
check Any.new.CREATE.^name, 'Any', 'CREATE on an Any instance';
my $u;
check $u.CREATE.gist, 'Any.new', 'CREATE on an untyped undefined variable';

# a user class: from the type, from an instance, from inside a method
class A { has $.x = 5; has @.l = 1, 2; method again { self.CREATE } }
check A.CREATE.x, Any, 'CREATE runs no attribute initializer';
check A.new(x => 3).CREATE.^name, 'A', 'CREATE on a user-class instance';
check A.new(x => 3).CREATE.x, Any, '…with its attributes unset';
check A.new.again.l, [], 'self.CREATE in a method called on an instance';
check (A.new.CREATE === A.new.CREATE), False, 'each CREATE is a fresh instance';

# a class's own CREATE wins, on the type object too, and is inherited
class P { method CREATE { "custom" } }
class P2 is P { }
check P.CREATE, 'custom', "a class's own CREATE, on the type";
check P.new.CREATE, 'custom', "a class's own CREATE, on an instance";
check P2.CREATE, 'custom', 'an inherited CREATE';
class Q { method CREATE { callsame } }
check Q.CREATE.^name, 'Q', 'callsame from a custom CREATE reaches the allocator';

# built-in types and values: an instance of the invocant's type
check Int.CREATE, 0, 'Int.CREATE';
check 42.CREATE, 0, 'CREATE on an Int';
check Str.CREATE.^name, 'Str', 'Str.CREATE';
check "x".CREATE.^name, 'Str', 'CREATE on a Str';
check Hash.CREATE.^name, 'Hash', 'Hash.CREATE';
check {a => 1}.CREATE.elems, 0, 'CREATE on a Hash is empty';
check Array.CREATE.^name, 'Array', 'Array.CREATE';
check [1, 2].CREATE.elems, 0, 'CREATE on an Array is empty';
check (1, 2).CREATE.^name, 'List', 'CREATE on a List';
check (a => 1).CREATE.^name, 'Pair', 'CREATE on a Pair';
check (1..3).CREATE.^name, 'Range', 'CREATE on a Range';
check Bool.CREATE, False, 'Bool.CREATE';
check Date.CREATE.^name, 'Date', 'Date.CREATE (its constructor needs a date)';
check Date.today.CREATE.^name, 'Date', 'CREATE on a Date';
check Cool.CREATE.^name, 'Cool', 'Cool.CREATE is a Cool, not a Str';
check Positional.CREATE.^name, 'Positional', 'a built-in role puns';
check Exception.CREATE.^name, 'Exception', 'Exception.CREATE';

# subclassed built-ins, roles and mixins
class H is Hash { }
check H.new.CREATE.^name, 'H', 'CREATE on a Hash subclass instance';
class I is Int { }
check I.new(5).CREATE.^name, 'I', 'CREATE on an Int subclass instance';
role R { has $.r = 1 }
check R.CREATE.^name, 'R', 'a user role puns';
check R.new.CREATE.r, Any, 'CREATE on a punned role instance';
check (5 but R).CREATE.^name, 'Int+{R}', 'CREATE on a mixin';

if @fail { .note for @fail; die "{+@fail} failure(s)" }
say 'PASS';
