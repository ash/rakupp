# Regression: a Perl-5 regex (`rx:P5/…/`) is a foreign language inside a Raku
# program, and it was mishandled at both ends.
#
# THE LEXER read a P5 `[ … ]` as Raku, where a `"` opens a string. So
# HTTP::Tinyish::Base's header-name token
#
#     rx:P5/[^][\x00-\x1f\x7f()<>@,;:\\"\/?={} \t]+/
#
# opened a string at that quote which ran to the end of the file, and the module
# was reported as an unterminated string 30 lines further on. In Perl 5 the
# brackets ARE the character class: nothing inside opens anything, and a `]` in
# first position (`[]…]`, `[^]…]`) is itself a member.
#
# THE INTERPOLATION then pasted a regex VALUE into the host pattern as text.
# Across flavours that changes its meaning — `[a-z]+` read as Raku is a group
# over `a`, `-`, `z` — so a spliced P5 regex quietly matched the wrong thing. And
# the SUBSTITUTION path did not splice regex values at all: it had its own copy
# of the interpolation loop, which quotemeta'd the value's stringification, so
# `s/^($token) \: ' '?//` searched for the literal text "regex Regex". A splice
# now carries its own syntax with it and is compiled by its own front-end.
#
# Verified line by line against Rakudo, which prints the same as this file
# asserts. Contract: exit 0 + last line PASS.
my @fail;

sub ok($desc, $got, $want = True) { @fail.push("$desc (got {$got.raku})") unless $got eqv $want }
sub is($desc, $got, $want) { @fail.push("$desc (got {$got.raku}, want {$want.raku})") unless $got eq $want }

# ---- the lexer: a P5 class holds characters, quotes included ----
my $token = rx:P5/[^][\x00-\x1f\x7f()<>@,;:\\"\/?={} \t]+/;   # HTTP::Tinyish::Base
is('the real token',   ("Content-Type: text/plain" ~~ $token).Str, 'Content-Type');
is('a double quote',   ('ab"cd' ~~ rx:P5/[abc"d]+/).Str, 'ab"cd');
is('a single quote',   ("a'b"   ~~ rx:P5/[ab']+/).Str,   "a'b");
is('leading ]',        ('a]b'   ~~ rx:P5/[]ab]+/).Str,   'a]b');
is('leading ] after ^',('a^b'   ~~ rx:P5/[^]b]+/).Str,   'a^');
is('a class of just ]',('x]y'   ~~ rx:P5/[]]/).Str,      ']');
is('…and it ENDS there',('ab'   ~~ rx:P5/[]a]b/).Str,    'ab');
is('] then ^ member',  ('a^]b'  ~~ rx:P5/[]^a]+/).Str,   'a^]');
is('negated, ] member',('a]b'   ~~ rx:P5/[^]b]+/).Str,   'a');
is('[ is a member',    ('a[b'   ~~ rx:P5/[ab[]+/).Str,   'a[b');
is('braces are members',('a}b{' ~~ rx:P5/[ab{}]+/).Str,  'a}b{');

# a P5 class still ends at its own `]`, and the delimiter still ends the regex
is('class then more',  ('abX'   ~~ rx:P5/[ab]+X/).Str,   'abX');
ok('quote is literal', so ('a"b' ~~ rx:P5/a"b/));

# ---- interpolation: a regex VALUE is a sub-pattern, in its own syntax ----
my $p5 = rx:P5/[a-z]+/;
my $rk = rx/<[a..z]>+/;
is('P5 value in a Raku match',   ("abc: 1" ~~ /^($p5)/).Str, 'abc');
is('Raku value in a Raku match', ("abc: 1" ~~ /^($rk)/).Str, 'abc');
my $d = rx:P5/\d+/;
is('P5 value in a P5 pattern',   ("x42y" ~~ rx:P5/x$d/).Str, 'x42');

# …and in a SUBSTITUTION, which quoted the value as literal text instead
my $h = "abc: 1";
ok('Raku value in s///', so ($h ~~ s/^($rk) \: ' '?//));
is('…substituted',       $h, '1');

$h = "abc: 1";
ok('P5 value in s///',   so ($h ~~ s/^($p5) \: ' '?//));
is('…substituted',       $h, '1');

$h = "Set-Cookie: a=1";                                        # the module's own line
ok('the HTTP::Tinyish line', so ($h ~~ s/^($token) \: ' '?//));
is('…leaves the value',  $h, 'a=1');

# the splice is ONE atom: a quantifier after it binds the whole thing
is('splice quantifies as a unit', ("abab!" ~~ /^[$rk]+/).Str, 'abab');

# a plain string variable still interpolates as quoted LITERAL text, not source
my $lit = 'a.c';
ok('a Str is literal',   so ('a.c' ~~ /^$lit$/));
ok('…and not a pattern', so ('abc' ~~ /^$lit$/), False);

if @fail { note "FAIL: $_" for @fail; say "FAIL" }
else { say "PASS" }
