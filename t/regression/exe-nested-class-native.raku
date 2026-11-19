# Regression guard: a class a routine or block declares compiles under --exe,
# and three answers compiled code gave wrongly.
#
# The native class tables were built from the top-level declarations only, so
# a class inside a routine or block refused the whole program (the first
# blocker of 92 Roast files). codegen/NestedClass.cpp lifts one to the top
# when nothing the program can see changes: a class or grammar whose name no
# other type has, whose body runs nothing, which is not the value of its block
# and which names nothing the code around it declares. The rest stay refused.
#
# Three wrong answers the newly native Roast files showed, each wrong in a
# top-level program too:
#  - `$x ~~ EXPR` evaluated EXPR with the outer `$_`, not $x
#    (S03-smartmatch/any-any.t)
#  - a list's .gist (and `say @objects`) ignored a class's own `gist`: a
#    compiled method is a builtin Code, which objMethodStrHook passed over
#  - `@a[lazy ^N]` was N elements, padded, where it ends with the array
#    (S03-sequence/nonnumeric.t)
# This file must compile natively (t/run.raku's list) and print the
# interpreter's answers, which are Rakudo 2026.09's.

my @got;
sub o(**@a) { @got.push: @a.map(*.Str).join(' ') }

sub counter($n) {
    my class Counter { has $.n; method twice { $!n * 2 } }
    Counter.new(n => $n).twice
}
my $top = 10;
sub uses-top() { class UsesTop { method t { $top + 1 } }; UsesTop.t }

sub topic-match() {   # (a sub's own `$_`: the loop's is read-only)
    $_ = 42;
    my $x;
    'abc' ~~ ($x = $_);
    o $x, $_;
    'defg' !~~ ($x = $_);
    o $x, $_;
    o 4 ~~ $_ * 1, 5 ~~ Int;
}

for ^3 {
    {
        class Dog { has $.name; method bark { "woof from $!name" } }
        class Puppy is Dog { method bark { callsame() ~ '!' } }
        o Dog.new(name => 'Rex').bark;
        o Puppy.new(name => 'Bit').bark, Puppy.^name, Puppy ~~ Dog;
    }
    o counter(4), counter(5), uses-top();
    {
        grammar Digits {
            token TOP { <d>+ }
            token d { \d }
        }
        o Digits.parse('123')<d>.elems;
    }
    {
        class Nested::Name { method m { 'q' } }
        o Nested::Name.m, Nested::Name.^name;
    }
    {
        class H { has $.y = 5; method succ { H.new(y => $.y + 1) }; method gist { "H$!y" } }
        my @h = H.new, H.new(y => 6);
        o @h.gist, (H.new ... *.y > 7).gist, (a => H.new).gist;
    }
    topic-match();
    my @three = 1, 2, 3;
    o ('A' ... 'ZZ')[lazy ^1000].elems, @three[lazy ^5].elems, (1, 2, 3)[^5].elems;
}

my @want = flat (q:to/WANT-END/.lines) xx 3;
woof from Rex
woof from Bit! Puppy True
8 10 11
3
q Nested::Name
[H5 H6] (H5 H6 H7 H8) a => H5
abc 42
defg 42
True True
702 3 5
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
