# Regression: the control escapes inside a QUOTED span in a regex.
#
# `/ "\t" /` was decoded, `/ "\b" /` was not — it fell through to "keep the
# character" and became the letter b. That is the worst possible failure for
# this one, because `\b` LOOKS like a word boundary to anyone reading it (Raku
# spells that `<|w>`; in Raku `\b` is BACKSPACE, in a string and in a regex
# alike). String::CamelCase's wordsplit alternation contains a literal "\b", so
# "year_bbs" split on every b and answered ("year", "s").
#
# \e, \f, \a and \c[…] were missing the same way.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $bs  = "a" ~  8.chr ~ "b";   # BACKSPACE
my $esc = "a" ~ 27.chr ~ "b";   # ESCAPE
my $ff  = "a" ~ 12.chr ~ "b";   # FORM FEED
my $bel = "a" ~  7.chr ~ "b";   # BELL

check so($bs  ~~ / "\b" /), True,  '\b in a quoted span is backspace';
check so("abb" ~~ / "\b" /), False, 'and is emphatically not the letter b';
check so($esc ~~ / "\e" /), True,  '\e is escape';
check so("aeb" ~~ / "\e" /), False, 'not the letter e';
check so($ff  ~~ / "\f" /), True,  '\f is form feed';
check so("afb" ~~ / "\f" /), False, 'not the letter f';
check so($bel ~~ / "\a" /), True,  '\a is bell';
check so("azb" ~~ / "\a" /), False, 'and matches nothing in a string without one';

# The escapes that already worked have to keep working.
check so("a\tb" ~~ / "\t" /), True, '\t still tab';
check so("a\nb" ~~ / "\n" /), True, '\n still newline';
check so("a\rb" ~~ / "\r" /), True, '\r still return';
check so("a\0b" ~~ / "\0" /), True, '\0 still NUL';
check so("A"    ~~ / "\x41" /), True, '\x41 still hex';

# \c names or numbers a character, as it does in a qq string.
check so("A"   ~~ / "\c[LATIN CAPITAL LETTER A]" /), True, '\c[NAME] names a character';
check so($esc  ~~ / "\c[ESCAPE]" /),                 True, 'including a control one';
check so("a\nb" ~~ / "\c[10]" /),                    True, '\c[10] numbers one';
check ("AB" ~~ / "\c[LATIN CAPITAL LETTER A,LATIN CAPITAL LETTER B]" /).Str,
      'AB', 'a comma list builds a multi-character literal';

# A SINGLE-quoted span keeps its backslash — that rule must not have moved.
check so("a\\bc" ~~ / '\b' /), True,  'single quotes keep backslash-b literal';
check so($bs     ~~ / '\b' /), False, 'so they do not match a backspace';

# The whole reason this was found: String::CamelCase's wordsplit.
sub wordsplit(Str $given) {
    $given.split(/
        <[_ \- \s]>+
        | "\b"
        | <?after <-[A ..Z]>> <?before <:Lu>>
        | <?after <:Lu>> <?before <:Lu> <:Ll>>
    /, :skip-empty).List
}
check wordsplit('AD'),               ('AD',),                     'a bare acronym is one word';
check wordsplit('YearBBS'),          ('Year', 'BBS'),             'trailing acronym';
check wordsplit('ClientADClient'),   ('Client', 'AD', 'Client'),  'acronym in the middle';
check wordsplit('year_bbs'),         ('year', 'bbs'),             'underscores, and no b eaten';
check wordsplit('client_ad_client'), ('client', 'ad', 'client'),  'three of them';
check wordsplit('ADClient-HogeFuga'),('AD', 'Client', 'Hoge', 'Fuga'), 'both separators at once';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
