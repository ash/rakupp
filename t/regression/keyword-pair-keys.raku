# From the Cognates port (docs/rakupp-findings findings 4 and 9), two parser bugs
# that both fail confusingly:
#
#   4. `method => 'GET'` did not parse. A fat arrow AUTO-QUOTES the identifier on
#      its left, so any identifier is a valid key — but the parser committed to
#      `method` as the start of a declaration. Worse, the error was reported at the
#      NEXT top-level construct's line, so bisecting was the only way to find it.
#
# Finding 9 (a declared `sub ms`/`rx`/`tr` losing to the quoting syntax) is NOT
# fixed and is not tested here: see docs/dev/QUOTE-WORD-SHADOWING.md.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# Every identifier auto-quotes before =>, keywords and term-words included.
check((method => 1).key, 'method', 'method => is a pair key');
check((sub    => 1).key, 'sub',    'sub =>');
check((for    => 1).key, 'for',    'for =>');
check((while  => 1).key, 'while',  'while =>');
check((do     => 1).key, 'do',     'do =>');
check((class  => 1).key, 'class',  'class =>');
check((if     => 1).key, 'if',     'if =>');
check((True   => 1).key, 'True',   'True => auto-quotes to a Str, not a Bool');
check((False  => 1).key, 'False',  'False => likewise');
check((Nil    => 1).key, 'Nil',    'Nil =>');

class R { has $.method; has $.class }
my $r = R.new(method => 'GET', class => 'X');
check($r.method, 'GET', 'as a named argument');
check($r.class,  'X',   'and a second one');

# Every quote form still lexes as a quote.
check(q{hi},                  'hi',    'q{} still quotes');
check(qq/a{1+1}c/,            'a2c',   'qq// still interpolates');
check(('a7' ~~ m/\d/).Str,    '7',     'm// still matches');
check((rx/ \d /).^name,       'Regex', 'rx// is still a Regex');
check('aXb'.trans('X' => 'Y'), 'aYb',  'trans still works');
check(qw{a b}.elems,           2,      'qw{} still splits words');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
