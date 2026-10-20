# BROKE: a scalar or a type whose name starts with a letter beyond ASCII lost
# what its name stood for (found 2026-10-19 by FatRoast, which renames Roast's
# identifiers into other scripts; S03-binding and S12 diverged only by name).
#
#   my $ч = 1; my $y := $ч; $ч = 2; say $y;      # 1 — a copy, not an alias
#   class Еоо { }; my Еоо $foo; say $foo;       # (Any) — the type was gone
#
# About forty sites decided "a plain variable, no twigil" with
# `ascii::isalpha(name[1]) || name[1] == '_'`, and for `$ч` name[1] is a UTF-8
# lead byte, so binding took it for a special and copied. Another forty decided
# "a type, not a native" with `ascii::isupper(type[0])`, so `my Еоо $x` was an
# untyped `$x`: no (Еоо) default, no `.= new`, no type check on assignment, and
# `my Еоо @a` had no `.of`.
#
# FIXED: nameLetterAt (src/Ast.h) takes any byte >= 0x80 after the sigil for a
# letter, since twigils and specials are ASCII and the lexer lets only a letter
# start a name there; the match cursor `$¢` (C2 A2) stays a special, which `$µ`
# (C2 B5, a letter) must not be mistaken for. typeNameLead (src/Ast.h) takes a
# first byte >= 0x80 for a type, since every native type is spelled in ASCII.
#
# Cyrillic, Latin-1, CJK and astral (𝐚, 𝐀: 4-byte) names; every expectation
# checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }
sub refusal(&code) { try { code(); return 'lived' }; $!.^name }

# --- scalar binding: `$y := $NAME` aliases, both ways round ------------------
{ my $ч = 1; my $y := $ч; $ч = 2; ck($y, 2, 'Cyrillic: a write to $ч shows through $y');
  $y = 3; ck($ч, 3, 'Cyrillic: …and a write to $y through $ч'); ck($y =:= $ч, True, 'Cyrillic: =:=') }
{ my $é = 1; my $y := $é; $é = 2; ck($y, 2, 'Latin-1: a write to $é shows through $y');
  ck($y =:= $é, True, 'Latin-1: =:=') }
{ my $甲 = 1; my $y := $甲; $甲 = 2; ck($y, 2, 'CJK: a write to $甲 shows through $y');
  ck($y =:= $甲, True, 'CJK: =:=') }
{ my $𝐚 = 1; my $y := $𝐚; $𝐚 = 2; ck($y, 2, 'astral: a write to $𝐚 shows through $y');
  ck($y =:= $𝐚, True, 'astral: =:=') }
{ my $x = 1; my $ж := $x; $x = 5; ck($ж, 5, 'a non-ASCII name as the bound side');
  $ж = 7; ck($x, 7, '…and written through') }
{ my $µ = 1; my $y := $µ; $µ = 2; ck($y, 2, '$µ (C2 B5) is a letter, not the cursor $¢ (C2 A2)') }
{ my $_ч = 1; my $y := $_ч; $_ч = 2; ck($y, 2, 'an underscore before the letter') }
{ my $seen; "abc" ~~ / a { $seen = $¢.^name } /; ck($seen, 'Match', '$¢ is still the match cursor') }

# …the same inside a routine, where the names live in pad slots
sub in-a-sub() {
    my $ч = 1; my $é = 1; my $甲 = 1; my $𝐚 = 1;
    my $a := $ч; my $b := $é; my $c := $甲; my $d := $𝐚;
    $ч = $é = $甲 = $𝐚 = 9;
    ($a, $b, $c, $d, $a =:= $ч, $d =:= $𝐚)
}
ck(in-a-sub(), (9, 9, 9, 9, True, True), 'binding inside a sub, all four scripts');

# --- element binding: `@a[i] := $NAME`, `%h{k} := $NAME` -----------------------
{ my @a = <a b c>; my $é = "d"; @a[1] := $é; $é = "e"; ck(@a[1], 'e', 'Latin-1: @a[1] := $é aliases');
  @a[1] = 'f'; ck($é, 'f', '…both ways') }
{ my @a = <a b c>; my $ч = "d"; @a[0] := $ч; $ч = "e"; ck(@a[0], 'e', 'Cyrillic: @a[0] := $ч aliases') }
{ my @a = <a b c>; my $甲 = "d"; @a[2] := $甲; $甲 = "e"; ck(@a[2], 'e', 'CJK: @a[2] := $甲 aliases') }
{ my %h; my $𝐚 = 1; %h<k> := $𝐚; $𝐚 = 2; ck(%h<k>, 2, 'astral: %h<k> := $𝐚 aliases');
  ck(%h<k> =:= $𝐚, True, '…and is the same container') }

# --- typed declarations ------------------------------------------------------
class Еоо { }
class Éa { has $.n = 1 }
class 甲 { }
class 𝐀 { }
class ёлка { }   # lower case, but not ASCII: still a type, never a native

{ my Еоо $foo; ck($foo.^name, 'Еоо', 'Cyrillic: my Еоо $foo holds (Еоо)'); ck($foo === Еоо, True, '…the type object itself') }
{ my Éa $x; ck($x.^name, 'Éa', 'Latin-1: my Éa $x holds (Éa)') }
{ my 甲 $x; ck($x.^name, '甲', 'CJK: my 甲 $x holds (甲)') }
{ my 𝐀 $x; ck($x.^name, '𝐀', 'astral: my 𝐀 $x holds (𝐀)') }
{ my ёлка $x; ck($x.^name, 'ёлка', 'a lower-case non-ASCII class is a type') }

ck((try { my Éa $x .= new; ($x.^name, $x.defined, $x.n) }), ('Éa', True, 1), 'Latin-1: my Éa $x .= new');
ck((try { my Еоо $x .= new; ($x.^name, $x.defined) }), ('Еоо', True), 'Cyrillic: my Еоо $x .= new');
ck((try { my 𝐀 $x .= new; $x.defined }), True, 'astral: my 𝐀 $x .= new');

ck(refusal({ my Еоо $x = 1 }), 'X::TypeCheck::Assignment', 'Cyrillic: a typed scalar refuses an Int');
ck(refusal({ my 甲 $x; $x = "s" }), 'X::TypeCheck::Assignment', 'CJK: …on a later assignment too');
ck(refusal({ my Éa $x = Éa.new }), 'lived', 'Latin-1: …and takes its own type');
{ my Еоо $x = Еоо.new; $x = Nil; ck($x.^name, 'Еоо', 'Nil resets a typed scalar to its type') }

{ my Еоо @a; ck(@a.of.^name, 'Еоо', 'Cyrillic: my Еоо @a is an Array of Еоо');
  @a[2] = Еоо.new; ck(@a[0].^name, 'Еоо', '…its holes read as (Еоо)');
  ck(refusal({ @a.push(1) }), 'X::TypeCheck::Assignment', '…and it refuses an Int') }
{ my 𝐀 %h; ck(%h.of.^name, '𝐀', 'astral: my 𝐀 %h is a Hash of 𝐀') }

{ my class B { has Еоо $.e; has 甲 $.c is rw }
  ck(B.new.e.^name, 'Еоо', 'an attribute typed Еоо defaults to (Еоо)');
  ck(refusal({ B.new(e => 3) }), 'X::TypeCheck::Assignment', '…and refuses an Int');
  ck(refusal({ B.new.c = 1 }), 'X::TypeCheck::Assignment', 'an rw attribute typed 甲 refuses an Int') }

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
