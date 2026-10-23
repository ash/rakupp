# BROKE: a variable whose name starts with a letter beyond ASCII was not a
# variable inside a regex or a replacement, and not a name after a twigil
# (found 2026-10-19 by FatRoast's names-v1, which renames Roast's identifiers
# into other scripts; still open on 2026-10-23):
#
#   my $é = rx/a/; say "a" ~~ /$é/;           # Nil — `$` then a literal `é`
#   my $ч = "x"; my $s = "q"; $s ~~ s/q/$ч/;  # "" — `$ч` read as `$`
#   class A { has $.ч = 1 }; say A.new.ч      # No such method 'ч'
#   say { $^ч + $^ш }(1, 2)                    # Undefined routine 'ч'
#
# The regex parser, the pattern pre-pass that splices a variable's value in
# and the replacement interpolator each scanned a name with ASCII tests, and
# the lexer took a twigil only before an ASCII letter (`$.ч` lexed as `$.`).
#
# FIXED: uniIdentCharAt / uniIdentEnd (src/Unicode.h) read a name the way the
# lexer does, by ID_Continue, and every scanner of a pattern or a replacement
# asks them; the lexer takes a twigil before any letter (unicodeLetterAt), and
# the parser's twigil tests ask nameLetterAt. A capture number still does not
# run into a letter after it (`(a)$0é`), and a digit still starts no name.
#
# Every expectation checked against Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }
sub m(Mu $m) { $m ?? ~$m !! Nil }

# --- a variable in a pattern ---------------------------------------------------
{ my $é = rx/a/; ck(m("a" ~~ /$é/), 'a', 'Latin-1 $é holding a regex') }
{ my @𝐯 = rx/a/, rx/b/; ck(m("b" ~~ /@𝐯/), 'b', 'astral @𝐯 of regexes') }
{ my $ч = "x"; ck(m("axb" ~~ /a $ч b/), 'axb', 'Cyrillic $ч as a literal') }
{ my $ч = "x"; ck(m("axb" ~~ /a "$ч" b/), 'axb', '…inside a double-quoted span') }
{ my $甲 = rx/\d+/; ck(m("a12" ~~ /a <$甲>/), 'a12', 'CJK <$甲> as a subrule') }
{ my @ш = <p q>; ck(m("zq" ~~ /z <@ш>/), 'zq', '<@ш> as subrules') }
{ my $é-ü = "h"; ck(m("h" ~~ /$é-ü/), 'h', 'a hyphen joins a non-ASCII name') }
{ ck(m("aaé" ~~ /(a)$0é/), 'aaé', 'a capture number stops before a letter') }
{ my $*ж = "d"; ck(m("d" ~~ /$*ж/), 'd', 'a dynamic $*ж') }

# --- a variable in a replacement -----------------------------------------------
{ my $ч = "x"; my $s = "q"; $s ~~ s/q/$ч/; ck($s, 'x', 's/q/$ч/') }
{ my %ü = k => "v"; my $s = "k"; $s ~~ s/k/%ü<k>/; ck($s, 'v', 's/k/%ü<k>/') }
{ my @é = <x y>; my $s = "z"; $s ~~ s/z/@é[1]/; ck($s, 'y', 's/z/@é[1]/') }

# --- a name after a twigil -----------------------------------------------------
{ my class A { has $.ч = 1; has @.é = 1, 2; has %!ü = a => 3;
               method m { $!ч + @!é.sum + %!ü<a> } }
  ck(A.new.ч, 1, 'has $.ч makes an accessor');
  ck(A.new(ч => 7).ч, 7, '…and a named argument');
  ck(A.new.é, [1, 2], 'has @.é');
  ck(A.new.m, 7, '$!ч, @!é and %!ü inside a method') }
{ my $*ч = 3; sub f { $*ч }; ck(f(), 3, 'my $*ч seen from a callee') }
ck({ $^ч ~ $^ш }("a", "b"), 'ab', 'placeholders $^ч $^ш');
ck({ $:ч }(ч => 5), 5, 'named placeholder $:ч');

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
