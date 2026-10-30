# Regression: the q family takes the punctuation delimiters a bare `m` and `s`
# already did — `q+…+`, `q*…*`, `qq^…^`, `Q=…=`, `qw+…+` — when the delimiter
# sits tight against the keyword and closes on the same line. Only `| ! ~ /`,
# quotes and brackets were read as delimiters, so `q+{{planet}}+`, the way
# Template::Mustache's spec tests write a lambda full of braces, was a parse
# error and the whole spec run (t/91-specs, t/92-specs-file) died loading it.
# A pair stays a pair (`q=>1`). `; , .` are left out, though Rakudo takes them:
# they are what follows a sigilless term named q (`|q` below), which this lexer
# may not have seen declared.
# Every expectation below is Rakudo's.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

ck q+{{planet}}+,          '{{planet}}',  'q+…+';
ck q+a\+b+,                'a+b',         'a backslashed delimiter is the delimiter';
ck q*abc*,                 'abc',         'q*…*';
ck q=abc=,                 'abc',         'q=…=';
ck q%abc%,                 'abc',         'q%…%';
ck q^abc^,                 'abc',         'q^…^';
ck q&abc&,                 'abc',         'q&…&';
ck q@abc@,                 'abc',         'q@…@';
ck q?abc?,                 'abc',         'q?…?';
ck Q*a\b*,                 'a\b',         'Q keeps its backslash';
ck qq*x{1+1}y*,            'x2y',         'qq interpolates';
ck qw+a b+,                <a b>,         'qw+…+';
ck qqw^a b^,               <a b>,         'qqw^…^';
ck 'sub { q+|planet| => {{planet}}+ }'.EVAL.(), '|planet| => {{planet}}', 'inside EVAL, as the spec tests use it';

my %h = q=>1;
ck %h, {q => 1}, 'q=> is still a pair';
# a CAPTURE parameter named q is a term, and `q;` ends its statement
my $cap;
my &f = -> \s, |q { $cap = q; q.elems };
ck f(1, 2, 3), 2, 'a `|q` capture is a term';
ck $cap.list, (2, 3), '…and `q;` after it ends the statement';

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
