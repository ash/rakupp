# Regression: a `my token` / `my rule` / `my regex` held in a variable is a
# pattern to `<$var>`, `$var` and `<{ … }>`.
#
# `my $p = &NAME` binds the Callable the declaration made, not a Regex value,
# and the interpolation read its `.Str` as the pattern: `/<$p>/` matched
# nothing and said nothing. `<&NAME>`, `<NAME>`, `~~ $p` and an anonymous
# `token { … }` in a variable all worked. URL::Find picks its scheme and host
# tokens that way (`my $base = $ascii ?? &baseascii !! &base; … <$base> …`),
# found no URL at all, and failed its own suite, so Data::ExampleDatasets,
# which needs it, would not install — and with it Mathematica::Serializer's
# examples/Serializer-profiling.raku would not run. The value now stands for
# the source it was declared with, a token's ratchet and a rule's sigspace
# kept to its own text.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub m($match) { $match ?? ~$match !! Nil }

my token plain   { http }
my token proto   {:i [http|https|ftp] }
my regex rplain  { http }
my token greedy  { a* }
my regex backtr  { a* }
my rule  words   { a b }

my $tk = &plain;
my $pi = &proto;
my $rr = &rplain;
my $gr = &greedy;
my $bt = &backtr;
my $ru = &words;

check '<$var> of a token',       m('http' ~~ / <$tk> /),        'http';
check '<$var> of a regex',       m('http' ~~ / <$rr> /),        'http';
check 'its own :i',              m('HTTP' ~~ / <$pi> /),        'HTTP';
check 'anchored around it',      m('http://' ~~ /^ <$tk> '://' /), 'http://';
check 'twice in a row',          m('httphttp' ~~ /^ <$tk> <$tk> $/), 'httphttp';
check 'a token ratchets',        m('aaa' ~~ /^ <$gr> a $/),      Nil;
check 'a regex backtracks',      m('aaa' ~~ /^ <$bt> a $/),      'aaa';
check 'a rule keeps sigspace',   m('a   b' ~~ / <$ru> /),        'a   b';
check 'an alias <x=$var>',       m(('http' ~~ / <x=$tk> /)<x>),   'http';
check '<{ &NAME }>',             m('http' ~~ / <{ &plain }> /),   'http';
check 'a bare $var splices',     m('http' ~~ / $tk /),           'http';
check '…and ratchets there too', m('aaa' ~~ /^ $gr a $/),        Nil;
check 'what always worked: <&NAME>', m('http' ~~ / <&plain> /),  'http';
check 'what always worked: ~~',  m('http' ~~ $tk),               'http';

# as URL::Find has it: the token chosen at run time, in a regex built there
{
    my token baseascii { [ <[a..z A..Z 0..9 \- . ]> ]+ }
    my token base      { [ <:Number + :Letter + [ . - ]> ]+ }
    sub find-urls(Str $s, :$ascii) {
        my $b = $ascii ?? &baseascii !! &base;
        my $p = &proto;
        my $url = regex { <$p> '://' <$b> };
        $s.comb($url).List
    }
    check 'URL::Find-shaped', find-urls('see http://правительство.рф and ftp://a.b'),
        ('http://правительство.рф', 'ftp://a.b');
    check '…ascii only',      find-urls('see http://правительство.рф and ftp://a.b', :ascii),
        ('ftp://a.b',);
}

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
