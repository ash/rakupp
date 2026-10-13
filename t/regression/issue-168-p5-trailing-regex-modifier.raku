# Regression (#168): a Perl 5 trailing modifier on a bare regex, `/hello/i`,
# compiled as the regex with postfix `i` (the imaginary unit) applied, so the
# match ran silently case-sensitive; `m/hello/is` called Test's `is`. Rakudo
# refuses every one of the seven Perl 5 modifiers tight against the closer,
# whatever follows it, naming the FIRST letter and its Raku spelling.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;

for Q{"Hello" ~~ /hello/i}    => ('/i', ':i'),
    Q{"Hello" ~~ /hello/ix}   => ('/i', ':i'),
    Q{"a" ~~ /a/sin}          => ('/s', '. or \N'),
    Q{"a" ~~ /a/m}            => ('/m', '^^ and $$ anchors'),
    Q{"a" ~~ /a/x}            => ('/x', 'normal default whitespace'),
    Q{"a" ~~ /a/g}            => ('/g', ':g'),
    Q{"a" ~~ /a/c}            => ('/c', ':c or :p'),
    Q{"a" ~~ /a/e}            => ('/e', 'interpolated {...} or s{} = ... form'),
    Q{"Hello" ~~ m/hello/is}  => ('/i', ':i'),
    Q{rx/a/i}                 => ('/i', ':i'),
    Q{$_ = "a"; s/a/b/ig}     => ('/i', ':i'),
    Q{$_ = "a"; s/a/b/if}     => ('/i', ':i'),
    Q{$_ = "a"; tr/a/b/i}     => ('/i', ':i') -> (:key($src), :value(($old, $repl))) {
    my $got = 'compiled';
    EVAL $src;
    CATCH { default { $got = .^name eq 'X::Obsolete' ?? "{.old}|{.replacement}" !! .^name } }
    @fail.push("$src gave $got") unless $got eq "$old|$repl";
}

# the Raku spellings, and a regex with room after it, still work
@fail.push("leading :i") unless so EVAL(Q{"Hello" ~~ /:i hello/});
@fail.push("m:i") unless so EVAL(Q{"Hello" ~~ m:i/hello/});
@fail.push("space after") unless EVAL(Q{"a" ~~ /a/ ?? 1 !! 0}) == 1;
@fail.push("s:g") unless EVAL(Q{$_ = "aa"; s:g/a/b/; $_}) eq 'bb';
@fail.push("division") unless EVAL(Q{8/2/2}) == 2;

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
