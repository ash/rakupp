# Regression: `@a ||= …`, `@a &&= …` and `%h ||= …` stored the right-hand value
# ITSELF in the variable — a List, a Range — instead of assigning its elements
# into the Array or Hash, so the next `.push`/`.shift` died "Cannot call 'shift'
# on an immutable 'List'" and `%h ||= (a => 1)` lost its pairs. `cro run` picks
# service colours with `shift state @colors ||= <green yellow …>`.
# Contract: exit 0 + last line PASS.
my @fail;

sub next-colour() { shift state @colours ||= <green yellow> }
my @seen = next-colour() xx 3;
@fail.push("colours {@seen}") unless @seen eqv ["green", "yellow", "green"];

my @a; @a ||= <x y>; @a.push("z");
@fail.push("||= list gave {@a.raku}") unless @a eqv [<x y z>];
my @r; @r ||= 1..3; @r.push(4);
@fail.push("||= range gave {@r.raku}") unless @r eqv [1, 2, 3, 4];
my @n; @n ||= (1, (2, 3));
@fail.push("||= nested gave {@n.raku}") unless @n.elems == 2;
my @d = 1; @d &&= <m n>; @d.pop;
@fail.push("&&= gave {@d.raku}") unless @d eqv ["m"];
my %h; %h ||= (a => 1, b => 2); %h<c> = 3;
@fail.push("%h ||= gave {%h.raku}") unless %h eqv {a => 1, b => 2, c => 3};

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
