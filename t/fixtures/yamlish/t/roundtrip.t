#!raku

use v6;
use Test;
use YAMLish;

my @data = (
	"A string",
	42,
	42.005,
	True,
	False,
	'A string containing a single (") double quote.',
	"A string containing both 'single' and \"double\" quotes.",
	"A multiline\nstring.\n",
	"  A multiline \n   string with varying\n  indentation.\n",
	[1, 2, 3],
	{:a(1), :b(2), :c(True)},
	[1, 2, "three"],
	{:a("one"), :b(2), :c(42.005)},
	[],
	{:a[ ], :b("something"), :c[ ]},
	{:a[1, 2, 3], :b[<something else>], :c[42.005]},
	{},
	[{}, [], {:array[ ], :hash({})}, {:something(42.005), :real[3.14]}],
	{:version("1.0"), :extensions[ ]},
	{:int('1'), :hex('0xFF'), :oct('0o007'), :rat('3.14'), :float('2e3'), :inf('.inf'), :nan('.nan'), :null('~'), :alias('*foo'), :datetime('2016-07-17T15-18-23T+02'), :date('2016-07-15'), :anchor('&foo'),},
	{:int('-1'), :hex('-0xFF'), :oct('-0o007'), :rat('-3.14'), :float('-2e3'), :inf('-.inf'), :nan('-.nan'),},
	{ :puzzle({:version("0.1"), :extensions[]}), :game('two-digits'), :title('Two Digits'), :board[1, 2, 3, 10, 20, 40, 100, 200, 300] },
);

plan @data.elems;

for @data -> $v {
	is-deeply load-yaml(save-yaml($v)), $v;
}
