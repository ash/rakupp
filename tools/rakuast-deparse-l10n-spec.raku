#!/usr/bin/env rakudo
# The localized-`.DEPARSE` spec: WHICH keyword is asked for, WHERE, and under
# which category, one line per case.
#
# ENGINE-NEUTRAL, like tools/rakuast-deparse-spec.raku: run under Rakudo it
# produces docs/dev/findings/rakuast/deparse-l10n-2026.09.tsv, run under rakupp
# it has to produce the same file, and t/regression/rakuast-deparse-l10n.raku
# is that diff.
#
#     rakudo tools/rakuast-deparse-l10n-spec.raku > docs/dev/findings/rakuast/deparse-l10n-2026.09.tsv
#
# `.DEPARSE("RU")` mixes the dist's `RakuAST::Deparse::L10N::RU` into the
# deparser, and that role is one method: `xsyn($prefix, $key)`, asked at every
# keyword, answering the Russian spelling or `$key`. A real table hides most of
# what is asked — a key it does not have comes back unchanged — so the cases
# here hand `.DEPARSE` a role that answers every question with the question
# itself, `<prefix:key>`, and the output shows each one: that `for` before a
# block is `<block:for>` and after a statement `<modifier:for>`, that a method
# call's dot is asked as well as its name, that a list's comma is not. No dist
# has to be installed for it.
#
# Each case is one whose ENGLISH rendering already agrees between the engines;
# where it does not (a signature's spacing, `start`'s implicit `$!`/`$/`, a
# string's `{ $x }`), the localization would only repeat that difference.

use experimental :rakuast;

my role Trace { method xsyn($p, $k) { "<$p:$k>" } }
my ($x, $y, @a, %h);
my role R { }; my role RP[::T] { };

sub d($label, $src) { say $label ~ "\t" ~ $src.AST.DEPARSE(Trace).raku }

d 'scope-core',      Q[my $x = 1; say $x];
d 'block-modifier',  Q[for @a -> $n { @a.push($n) if $n mod 2 eq 0 }];
d 'if-elsif-else',   Q[if $x { say 1 } elsif $y { say 2 } else { say 3 }];
d 'unless-loops',    Q[unless $x { 1 }; while $x { 1 }; until $x { 1 }];
d 'given-when',      Q[given $x { when Int { 1 }; default { 2 } }];
d 'loop',            Q[loop (my $i = 0; $i < 3; $i++) { 1 }];
d 'repeat-until',    Q[repeat { $x++ } until $x >= 2];
d 'with-without',    Q[with $x { 1 }; without $y { 2 }];
d 'modifiers',       Q[1 unless $x; 2 while $x; 3 for @a; 4 with $x];
d 'try-catch',       Q[try { die "x"; CATCH { default { .message } } }];
d 'stmt-prefixes',   Q[gather { take 1 }; do { 1 }; lazy @a; react { whenever $x { 1 } }];
d 'phasers',         Q[ENTER { 1 }; LEAVE { 2 }; END { 3 }];
d 'multi-sub',       Q[multi sub f { return 2 * 2 }];
d 'class-members',   Q[class P { has $.v; method m { self.v }; submethod BUILD { 1 } }];
d 'role-module',     Q[my role R { method TWEAK { 1 } }; module M { 1 }];
d 'enum-subset',     Q[enum C <a b>; my subset S of Int where * > 0];
d 'infix-meta',      Q[$x = 1; $x += 1; $x R- 1; $x !eq $y; [max] @a; $x Z~ $y];
d 'infix-words',     Q[$x and $y; $x or $y; $x xor $y; $x leg $y; $x before $y];
d 'named-args',      Q[@a.first(:k, * > 1); @a.sort(:by(1)); @a.first(:!k); @a.sort(by => 1)];
d 'method-calls',    Q[@a.map(*.abs).elems; %h.keys; $x.^name; $x.?y];
d 'terms',           Q[True; False; now; rand; pi; exit];
d 'use-need-import', Q[use Test; need Test; import Test; use strict];
d 'list-comma',      Q[my $s = "ab"; my @l = 1, 2, 3];
d 'attribute-traits', Q[class AT { has $.x is rw; has $.z is required("why"); has $.v is built(False) }];
d 'package-traits',  Q[class PT is Int does R does RP[Str] is rw { has $.q }];
d 'our-sub',         Q[module MS { our sub f { 1 } }];
d 'param-traits',    Q[my $c = -> $a is rw, $b is copy, ($p, $q) { 1 }];
d 'if-binders',      Q[if $x -> $a { 1 } elsif $y -> $b { 2 } else -> $c { 3 }];
d 'loop-binders',    Q[with $x -> $a { 1 }; for @a -> Int $i { 1 }; for @a -> $j is rw { 1 }];
d 'pragmas',         Q[no strict; use lib "x"; use lib "a", "b"];
d 'dotty-assign',    Q[$x.=abs; $x .= substr(1); $x.=uc.lc];
