# Regression: two more mis-scans that swallowed whole files, and the `$/`
# postfix they led to.
#
# 1. A CHARACTER CLASS holds characters, not structure. An earlier fix stopped
#    `[` inside one from nesting; Pod::To::HTML showed that was half the story:
#
#        /<[ & < > " ' {   ]>/
#
#    the lone `<` left the angle depth open and the lone `{` opened a code block
#    that never closed, so the closing `/` stayed shielded and the scan ate 300
#    lines. Inside `<[…]>` nothing moves a counter — only `]` ends it.
#
# 2. `$/` takes a postfix like any other variable — `$/[0]`, `$/<k>`, `$/.Str`.
#    Both the string interpolator and the substitution replacement stopped at the
#    sigil, so `"$/[0]"` came out as the match followed by the literal `[0]`, and
#    HTTP::Tinyish's `s/…/$/[0]/` replaced the match with seven literal
#    characters.
# Contract: exit 0 + last line PASS.
my @fail;
sub ok($desc, $got, $want = True) { @fail.push("$desc (got {$got.raku})") unless $got eqv $want }

# ---- 1. every bracket inside a class is a member ---------------------------
ok('lone <',        so '<' ~~ /<[ < ]>/);
ok('lone {',        so '{' ~~ /<[ { ]>/);
ok('lone [',        so 'x' ~~ /<-[[]>/);
ok('the whole set', so '{' ~~ /<[ & < > " ' {   ]>/);
ok('set excludes',  so '&' ~~ /^<-[ & < > " ' {   ]>$/, False);
ok('set includes',  so '<' ~~ /^<[ & < > " ' {   ]>$/);
ok('braces pair',   so 'x' ~~ /<-[{}]>/);
ok('angles pair',   so 'x' ~~ /<-[<>]>/);

# structure OUTSIDE a class still works: groups nest, code blocks run,
# assertions and quotes all behave
ok('group nests',   so 'ab' ~~ /^ [ [ 'a' ] [ 'b' ] ] $/);
ok('code block',    so 'a'  ~~ / 'a' { ; } /);
ok('assertion',     so 'a'  ~~ / <[a]> /);
ok('quoted <',      so '<'  ~~ / '<' /);
my $seen = 0;
'aaa' ~~ / a { $seen++ } a /;
ok('block ran',     $seen > 0);

# ---- 2. $/ takes a postfix -------------------------------------------------
'abc' ~~ /(b)/;
ok('bare $/',        "$/",        'b');
ok('subscript',      "$/[0]",     'b');
ok('method call',    "$/.Str()",  'b');
ok('numbered still', "$0",        'b');

'key=value' ~~ /$<k>=(\w+) '=' $<v>=(\w+)/;
ok('named subscript', "$/<k>", 'key');

# …in a substitution replacement too
my $h = "HTTP/1.1 200 OK";
$h ~~ s/^(HTTP\/\d[\.\d]?) ' '/$/[0]-/;
ok('replacement $/[0]', $h, 'HTTP/1.1-200 OK');

my $b = 'abc';
$b ~~ s/(b)/[$/[0]]/;
ok('replacement bracketed', $b, 'a[b]c');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
