# Regression: strings that are views of a shared buffer (APPEND-PLAN.md) read
# exactly as flat ones do. `$k = $s; $s ~= x`, `$s = x ~ $s` and `.substr` of a
# long string share one buffer instead of copying, so this holds the cases where
# that could leak: two holders of one buffer growing it differently, a join
# that must still compose (é) or count CR LF as one, a non-ASCII slice, a view
# used as a hash key, in `eq` and a regex, a slice outliving its source's
# growth, numbers appended, and eight threads reading a view whose buffer the
# main thread keeps appending to.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $base = 'x' x 300;
{ my $s = $base; my $k = $s; $s ~= 'A'; my $j = $s; $s ~= 'B'; $j ~= 'C'; $k ~= 'D';
  ck(($s.chars, $j.chars, $k.chars, $s.substr(*-2), $j.substr(*-2), $k.substr(*-1)),
     (302, 302, 301, 'AB', 'AC', 'D'), 'three holders of one buffer grow it three ways') }
{ my $s = "\x[301]" ~ 'y' x 300; my $t = $s; $t = 'e' ~ $t;
  ck(($t.chars, $t.codes, $t.substr(0, 1).ord), (301, 301, 233), 'a prepended base composes with a leading mark') }
{ my $s = 'y' x 300; for ^3 { $s = 'é' ~ $s };
  ck(($s.chars, $s.codes, $s.substr(0, 3)), (303, 303, 'ééé'), 'non-ASCII prepended') }
{ my $s = 'z' x 300 ~ "\r"; my $k = $s; $s ~= "\n";
  ck(($s.chars, $k.chars, ($s ~ 'q').chars), (301, 301, 302), 'CR then LF appended is one character') }
{ my $s = "\n" ~ 'z' x 300; my $k = $s; $s = "\r" ~ $s;
  ck(($s.chars, $s.codes), (301, 302), 'CR prepended before LF is one character') }
{ my $s = 'ä' x 400; my $k = $s; $s ~= 'b'; my $t = $s.substr(100);
  ck(($t.chars, $t.substr(*-2), $s.substr(399, 2)), (301, 'äb', 'äb'), 'a non-ASCII slice') }
{ my $s = 'q' x 300; my $k = $s; $s ~= 'r'; $s = $s ~ $s;
  ck(($s.chars, $s.substr(300, 2)), (602, 'rq'), '$s ~ $s') }
{ my $s = 'k' x 300; my $k = $s; $s ~= 'end'; my %h = $s => 1;
  ck((%h{'k' x 300 ~ 'end'}, $s eq 'k' x 300 ~ 'end', so $s ~~ / k+ end $ /), (1, True, True),
     'a hash key, eq, a regex') }
{ my $s = 'é' x 600; my $n = 0; while $s.chars { $s = $s.substr(3); $n++ };
  ck($n, 200, 'a non-ASCII string cut down to nothing') }
{ my $s = 'm' x 600; my $k = $s; $s ~= 'X'; my $t = $s.substr(1, 599); $s ~= 'Y';
  ck(($t.chars, $t.substr(*-1), $s.substr(*-2)), (599, 'm', 'XY'), 'a slice keeps its text while its source grows') }
{ my $s = 'n' x 300; my $k = $s; $s ~= 42; $s ~= 1.5e0; $s ~= True;
  ck($s.substr(300), '421.5True', 'numbers appended to a shared string') }
{ my $s = 'p' x 1000; my $k = $s; $s ~= 'Q';
  my @r = (^8).map({ start { my $c = 0; for ^200 { $c += $k.chars }; $c } });
  for ^2000 { $s ~= 'Z' }
  ck(((await @r).sum, $s.chars), (1600000, 3001), 'threads read a view while its buffer grows') }

say $fails ?? "FAILED $fails" !! "PASS";
