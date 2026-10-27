# Regression: `:4m, :5s` died with "Malformed radix number".
#
# After `:4` the name is the pair's KEY — `:4m` is m => 4 — but the lexer only
# held a quote form back when the name was tight after the `:` itself. With
# a comma next, `m,` opened an `m,…,` match (and `rx,` an rx one), so the
# parser saw `:4` with nothing to convert. Timezones::ZoneInfo's tests write
# their times as `make-time(:2020y, :1M, :2d, :3h, :4m, :5s, …)`.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

sub pairs(*%h) { %h.sort.map({ "{.key}={.value}" }).join(',') }

ok pairs(:4m, :5s) eq 'm=4,s=5', ':4m, :5s is two pairs';
ok pairs(:4rx,:5s) eq 'rx=4,s=5', ':4rx,:5s is two pairs';
ok pairs(:2020y, :1M, :2d, :3h, :4m, :5s, :4wd) eq 'M=1,d=2,h=3,m=4,s=5,wd=4,y=2020',
   'the make-time call of Timezones::ZoneInfo';
my @l = (1, :2m, :3s);
ok @l.elems == 3 && @l[1].key eq 'm' && @l[1].value == 2, ':2m inside a list';
# …and the match forms themselves are untouched
my $x = 'abc';
ok ($x ~~ m,b,).Str eq 'b', 'm,…, is still a match';
ok ('a1' ~~ m/\d/).Str eq '1', 'm/…/ is still a match';
ok (:16<ff>) == 255, ':16<ff> is still a radix number';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
