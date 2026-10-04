# `my $c := @arr[0]; $c += 1` lost the write: `+=`, `~=` and `++` on a
# Proxy-bound scalar called FETCH with no arguments, and the compact array-slot
# proxy reads its target out of the proxy it is handed, so it read Any — `+=`
# stored 1 and `~=` stored "x" whatever the element held. An ordinary read
# (deproxy) already passed the proxy; the two OP= paths now read the same way.
use Test;
plan 9;

my @arr = 1, 2;
my $c := @arr[0];
$c += 1;       is-deeply @arr, [2, 2],     '+= through a bound element';
$c++;          is-deeply @arr, [3, 2],     '++';
--$c;          is-deeply @arr, [2, 2],     'prefix --';
$c ~= 'x';     is-deeply @arr, ['2x', 2],  '~=';

sub f { my @x = 5; my $y := @x[0]; $y *= 3; @x }
is-deeply f(), [15],                        'inside a sub';

my %h = a => 1;
my $d := %h<a>;
$d += 5;       is %h<a>, 6,                 'a bound hash slot still works';

my @log;
my $p := Proxy.new(FETCH => sub ($) { 42 }, STORE => sub ($, $v) { @log.push: $v });
$p += 1;       is @log[*-1], 43,            'a user Proxy with sub FETCH';
$p++;          is @log[*-1], 43,            '…and ++ on it';
my $q := Proxy.new(FETCH => method () { 7 }, STORE => method ($v) { @log.push: $v });
$q += 1;       is @log[*-1], 8,             'a user Proxy with method FETCH';
