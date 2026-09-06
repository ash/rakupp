# Grand Review 2026-09, batch B (parser/lexer) — every case verified against
# Rakudo before the fix:
#
#   * "$n.is-prime()" printed `7.is-prime()`: the interpolation chain's method
#     name stopped at the hyphen (the lexer's copy too, so a quote inside the
#     argument list ended the string). One chain scanner now serves `$/`, `$!`,
#     `$0`, `$<x>` and `$var` — "$0.uc()" interpolates, "$!.message" (no parens)
#     stays literal, as in Rakudo.
#   * 0b12 / 0o89 / 0xfg were accepted (1 / 0 / 15); "\c65" was the string
#     `c65`; "\xZZ" emitted a silent NUL; "\c[NO SUCH NAME]" emitted nothing.
#   * rx/ [ ']' ] / opened a string: a quote inside a `[ ]` GROUP is a quote
#     (only inside a character class is it a member); a `#` comment inside a
#     regex ended it at the delimiter it mentioned.
#   * a class named SS/ss was unusable (`SS.n` lexed as a substitution).
#   * `%x<` with no `>` was a silent zen slice — mid-file it ate the next statement.
#   * a statement-leading `{ $d.uc => 1 }` was a Block where the expression
#     form (and Rakudo) make a Hash: one block-or-hash rule now.
#   * `:name($n) is required` on the alias path was a user trait; `(:a<b c>).value`
#     was an Array, not a List; a heredoc opened on the last line was silently
#     empty; `any <# x>` read the `#` as a comment (three term classifiers, one set).
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
sub parse-fails(Str $code, $desc) {
    my $threw = False;
    try { EVAL $code; CATCH { default { $threw = True } } }
    check($threw, True, $desc);
}

# -- interpolation chain ------------------------------------------------------
my $n = 7;
check("$n.is-prime()", "True", 'a hyphenated method name interpolates');
my $s = "abc";
check("$s.starts-with('a')", "True", 'hyphenated method with a single-quoted argument');
check("$s.starts-with("a")", "True", 'hyphenated method with a double-quoted argument (the lexer copy)');
check("$s.uc()", "ABC", 'a plain method call still interpolates');
check("$s.uc", "abc.uc", 'a bare .method stays literal');
"abc" ~~ /(a)(b)/;
check("$0.uc()", "A", '"$0.method()" interpolates');
"abc" ~~ /$<x>=(b)/;
check("$<x>.uc()", "B", '"$<x>.method()" interpolates');
try { die "boom" }
check("$!.message", "boom.message", '"$!.message" without parens stays literal');
check("$!.message()", "boom", '"$!.message()" with parens interpolates');
check("$s.uc().chars[0]", "ABC.chars[0]", 'a subscript after a bare .name does not commit it');
my $rg = 0..^10;
check("$rg.gist().int-bounds[0]", "^10.int-bounds[0]", '…the roast S02-types/range.t description shape');
check("$s.uc()[0]", "ABC", 'a subscript after a call continues the chain');

# -- numeric literals and string escapes -------------------------------------
parse-fails('0b12', '0b12 is refused');
parse-fails('0o89', '0o89 is refused');
parse-fails('0xfg', '0xfg is refused');
check(0b101, 5, '0b101 still parses');
check(0xFF, 255, '0xFF still parses');
check("\c65", "A", '"\c65" is the decimal codepoint form');
check("\c10".ord, 10, '"\c10" is a newline');
check("\c[LATIN SMALL LETTER A]", "a", '"\c[NAME]" still works');
check("\c[latin small letter a]", "a", 'character names are case-insensitive');
check("\x41", "A", '"\x41" still works');
parse-fails('"\xZZ"', '"\xZZ" is refused, not a silent NUL');
parse-fails('"\c[NOT A REAL CHARACTER NAME AT ALL]"', 'an unknown character name is refused');

# -- regex delimiter scanning --------------------------------------------------
check(?("]" ~~ rx/ [ ']' ] /), True, 'a quote inside a [ ] group is a quote');
check(?(")" ~~ m/ [ ')' | ']' ] /), True, 'a quoted closer inside a group');
check(?('"' ~~ / <-["]> /), False, 'a quote inside a character class is still a member');
check(?("ab" ~~ / a  # a comment mentioning / and }
                 b /), True, 'a # comment inside a regex runs to the end of the line');
grammar GComment { token TOP { a # closes the } here
                                b } }
check(?GComment.parse("ab"), True, 'a # comment inside a token body');

# -- a class named SS ---------------------------------------------------------
class SS { method n { 42 } }
check(SS.n, 42, 'SS.n is a method call, not a substitution');
my $t = SS;
check($t.^name, "SS", 'SS as a term');

# -- an unclosed angle subscript is an error ----------------------------------
parse-fails('my %x; my $v = %x<', '%x< at end of input is refused');
parse-fails("my \%x; my \$v = \%x<a;\nmy \$w = 1;", '%x<a; mid-file is refused, not a zen slice');

# -- block or hash, one rule ---------------------------------------------------
sub composer { my $d = "k"; { $d.uc => 1 } }
check(composer().^name, "Hash", 'a statement-leading composer with a computed key is a Hash');
check(composer()<K>, 1, '…with the computed key');
sub blocky { { say-nothing() } }
sub say-nothing { 5 }
check(blocky(), 5, 'a statement-leading block is still a block');

# -- parameters, colon-pair lists, heredocs, word lists ------------------------
sub req(:name($nm) is required) { $nm }
check(req(name => 3), 3, ':name($n) is required binds');
my $reqThrew = False;
try { EVAL 'req()'; CATCH { default { $reqThrew = True } } } # (EVAL: Rakudo refuses the call at compile time)
check($reqThrew, True, ':name($n) is required is required');
check((:a<b c>).value.^name, "List", '(:a<b c>).value is a List');
check((:a<b c>).value.elems, 2, '…of two words');
parse-fails("say q:to/END/;", 'a heredoc opened on the last line is refused');
check(?("#" eq any <# x>), True, '`any <# x>` is a word list, not a comment');

#   * (B2) `#`[ … ]` inside a regex is an EMBEDDED comment ending at its closer,
#     not at the end of the line; "\cA" is chr 1 ("\c@" NUL, "\c?" DEL); a
#     user-declared postfix must touch its operand, so `3 !< 2` with a
#     `sub postfix:<!>` in scope is still the negated `<`.
check("\c@".ords.List, (0,), '"\\c@" is NUL');
check("\cA".ords.List, (1,), '"\\cA" is chr 1');
check("\c?".ords.List, (127,), '"\\c?" is DEL');
check(("foo" ~~ /<[f] #`[comment] + [o]>/).Str, 'f', 'an embedded #`[…] comment inside a character class');
check(("foo" ~~ / f #`(cmt) o /).Str, 'fo', 'an embedded #`(…) comment inside a regex');
check(("foo" ~~ / f #`((cmt (x) )) o /).Str, 'fo', 'a doubled-opener embedded comment closes at its doubled closer');
check(("foo" ~~ / f # a line comment
                   o /).Str, 'fo', 'a line comment inside a regex');
{
    sub postfix:<!>($N) { [*] 2..$N }
    check(6!, 720, 'a user postfix touching its operand');
    check(3 !< 2, True, '`3 !< 2` with a postfix:<!> in scope is the negated <');
    check('a' !eq 'b', True, '`!eq` with a postfix:<!> in scope');
}

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
