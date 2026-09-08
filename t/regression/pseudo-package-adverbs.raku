# `UNIT::{…}:exists` and `:p` — subscript adverbs on a pseudo-package stash.
#
# Only MY:: and LEXICAL:: reached the adverb branch, and only for `:exists`, so
# `UNIT::{"&$_"}:exists` fell out of it and the adverb was read as a routine
# call ("Undefined routine 'exists'") — while the same thing in an `if`
# condition or inside parens was an outright parse error. Test::Output builds
# its whole EXPORT list that way and went 2/2 -> 0/2 in the module battery.
#
# UNIT:: asks the COMPILATION UNIT's scope, not the current one: a module's
# EXPORT sub runs in its own routine frame and asks about the file's symbols.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
sub greet() { 'hi' }
my $lex = 1;

check(UNIT::{'&greet'}:exists,      True,  'UNIT :exists finds a declared sub');
check(UNIT::{'&nope'}:exists,       False, 'UNIT :exists is False for a missing one');
check((UNIT::{'&greet'}:p).gist,    '&greet => &greet', 'UNIT :p is the key => value pair');
check((UNIT::{'&nope'}:p).elems,    0,     'UNIT :p on a miss is empty');
check(MY::{'$lex'}:exists,          True,  'MY :exists still answers');

# the shapes that used to be a PARSE ERROR rather than a wrong answer
my $in-if = 0;
if UNIT::{'&greet'}:exists { $in-if = 1 }
check($in-if,                       1,     ':exists works in an if condition');
check((UNIT::{'&greet'}:exists),    True,  ':exists works inside parens');

# from inside a routine, UNIT:: still sees the FILE's symbols (the EXPORT case)
sub probe() { UNIT::{'&greet'}:exists }
check(probe(),                      True,  'UNIT:: from inside a routine sees file scope');

# ordinary hash subscript adverbs are untouched
my %h = a => 1;
check(%h<a>:exists,                 True,     'hash :exists unchanged');
check((%h<a>:p).gist,               'a => 1', 'hash :p unchanged');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
