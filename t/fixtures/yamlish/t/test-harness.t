#!raku

use Test;

use YAMLish;

my @SCHEDULE = (
	{   name => 'Hello World',
		in   => [
			'--- Hello, World',
			'...',
		],
		out => "Hello, World",
	},
	{   name => 'Hello World 2',
		in   => [
			'--- \'Hello, \'\'World\'',
			'...',
		],
		out => "Hello, 'World",
	},
	{   name => 'Hello World 3',
		in   => [
			'--- "Hello, World"',
			'...',
		],
		out => "Hello, World",
	},
	{   name => 'Hello World 4',
		in   => [
			'--- "Hello, World"',
			'...',
		],
		out => "Hello, World",
	},
	{   name => 'Hello World 5',
		in   => [
			'--- >',
			'  Hello,',
			'    World',
			'...',
		],
		out => "Hello,\n  World\n",
	},
	{   name => 'Hello World Block',
		in   => [
			'--- |',
			'  Hello,',
			'    World',
			'...',
		],
		out => "Hello,\n  World\n",
	},
	{   name => 'Hello World 6',
		in   => [
			'--- >',
			'   Hello,',
			'  World',
			'...',
		],
		error => rx{Missing\s+'[.][.][.]'},
	},
	{   name => 'Simple array',
		in   => [
			'---',
			'- 1',
			'- 2',
			'- 3',
			'...',
		],
		out => [ 1, 2, 3 ],
	},
	{   name => 'Mixed array',
		in   => [
			'---',
			'- 1',
			'- \'two\'',
			'- "three\n"',
			'...',
		],
		out => [ 1, 'two', "three\n" ],
	},
	{   name => 'Hash in array',
		in   => [
			'---',
			'- 1',
			'- two: 2',
			'- 3',
			'...',
		],
		out => [ 1, { two => 2 }, 3 ],
	},
	{   name => 'Hash in array 2',
		in   => [
			'---',
			'- 1',
			'- two: 2',
			'  three: 3',
			'- 4',
			'...',
		],
		out => [ 1, { two => 2, three => 3 }, 4 ],
	},
	{   name => 'Nested array',
		in   => [
			'---',
			'- one',
			'-',
			'  - two',
			'  -',
			'   - three',
			'  - four',
			'- five',
			'...',
		],
		out => [ 'one', [ 'two', ['three'], 'four' ], 'five' ],
	},
	{   name => 'Nested hash',
		in   => [
			'---',
			'one:',
			'  five: 5',
			'  two:',
			'    four: 4',
			'    three: 3',
			'six: 6',
			'...',
		],
		out => {
			one => { two => { three => 3, four => 4 }, five => 5 }, six => 6
		},
	},
	{   name => 'Space after colon',
		in   => [ '---', 'spog: ', ' - 1', ' - 2', '...' ],
		out => { spog => [ 1, 2 ] },
	},
	{   name => 'Original YAML::Tiny test',
		schema => YAMLish::Schema::Extra,
		in   => [
			'---',
			'invoice: 34843',
			'date   : 2001-01-23',
			'bill-to:',
			'  given  : Chris',
			'  family : Dumars',
			'  address:',
			'    lines: |',
			'      458 Walkman Dr.',
			'      Suite #292',
			'    city   : Royal Oak',
			'    state  : MI',
			'    postal : 48046',
			'product:',
			'  - sku		 : BL394D',
			'    quantity	: 4',
			'    description : Basketball',
			'    price	   : 450.00',
			'  - sku		 : BL4438H',
			'    quantity	: 1',
			'    description : Super Hoop',
			'    price	   : 2392.00',
			'tax  : 251.42',
			'total: 4443.52',
			'comments: >',
			'  Late afternoon is best.',
			'  Backup contact is Nancy',
			'  Billsmer @ 338-4338',
			'...',
		],
		out => {
			'bill-to' => {
				'given'   => 'Chris',
				'address' => {
					'city'   => 'Royal Oak',
					'postal' => 48046,
					'lines'  => "458 Walkman Dr.\nSuite #292\n",
					'state'  => 'MI'
				},
				'family' => 'Dumars'
			},
			'invoice' => 34843,
			'date'	=> Date.new('2001-01-23'),
			'tax'	 => 251.42,
			'product' => [
				{   'sku'		 => 'BL394D',
					'quantity'	=> 4,
					'price'	   => 450.00,
					'description' => 'Basketball'
				},
				{   'sku'		 => 'BL4438H',
					'quantity'	=> 1,
					'price'	   => 2392.00,
					'description' => 'Super Hoop'
				}
			],
			'comments' => "Late afternoon is best. Backup contact is Nancy Billsmer @ 338-4338\n",
			'total' => 4443.52
		}
	},

	# Tests harvested from YAML::Tiny
	{   in	=> ['...'],
		name  => 'empty',
		error => rx{document\s+header\s+not\s+found}
	},
	{   in => [
			'# comment',
			'...'
		],
		name  => 'only_comment',
		error => rx{document\s+header\s+not\s+found}
	},
	{   out => Any,
		in  => [
			'---',
			'...'
		],
		name  => 'only_header',
		error => rx:i{Premature\s+end},
	},
	{   out => Any,
		in  => [
			'---',
			'---',
			'...'
		],
		name  => 'two_header',
		error => rx:i{Unexpected\s+start},
	},
	{   out => Any,
		in  => [
			'--- ~',
			'...'
		],
		name => 'one_undef'
	},
	{   out => Any,
		in  => [
			'---  ~',
			'...'
		],
		name => 'one_undef2'
	},
	{   in => [
			'--- ~',
			'---',
			'...'
		],
		name  => 'two_undef',
		error => rx{Missing\s+'[.][.][.]'},
	},
	{   out => 'foo',
		in  => [
			'--- foo',
			'...'
		],
		name => 'one_scalar',
	},
	{   out => 'foo',
		in  => [
			'---  foo',
			'...'
		],
		name => 'one_scalar2',
	},
	{   out =>  'foo',
		in => [
			'--- foo',
			'--- bar',
			'...'
		],
		name  => 'two_scalar',
	},
	{   out => ['foo'],
		in  => [
			'---',
			'- foo',
			'...'
		],
		name => 'one_list1'
	},
	{   out => [
			'foo',
			'bar'
		],
		in => [
			'---',
			'- foo',
			'- bar',
			'...'
		],
		name => 'one_list2'
	},
	{   out => [
			Any,
			'bar'
		],
		in => [
			'---',
			'- ~',
			'- bar',
			'...'
		],
		name => 'one_listundef'
	},
	{   out => { 'foo' => 'bar' },
		in  => (
			'---',
			'foo: bar',
			'...'
		),
		name => 'one_hash1'
	},
	{   out => {
			'foo'  => 'bar',
			'this' => Any,
		},
		in => [
			'---',
			'foo: bar',
			'this: ~',
			'...'
		],
		name => 'one_hash2'
	},
	{   out => {
			'foo' => [
				'bar',
				Any,
				'baz'
			]
		},
		in => [
			'---',
			'foo:',
			'  - bar',
			'  - ~',
			'  - baz',
			'...'
		],
		name => 'array_in_hash'
	},
	{   out => {
			'bar' => { 'foo' => 'bar' },
			'foo' => Any
		},
		in => [
			'---',
			'foo: ~',
			'bar:',
			'  foo: bar',
			'...'
		],
		name => 'hash_in_hash'
	},
	{   out => [
			{   'foo'  => Any,
				'this' => 'that'
			},
			'foo', Any,
			{   'foo'  => 'bar',
				'this' => 'that'
			}
		],
		in => [
			'---',
			'-',
			'  foo: ~',
			'  this: that',
			'- foo',
			'- ~',
			'-',
			'  foo: bar',
			'  this: that',
			'...'
		],
		name => 'hash_in_array'
	},
	{   out => ['foo'],
		in  => [
			'---',
			'- \'foo\'',
			'...'
		],
		name => 'single_quote1'
	},
	{   out => ['  '],
		in  => [
			'---',
			'- \'  \'',
			'...'
		],
		name => 'single_spaces'
	},
	{   out => [''],
		in  => [
			'---',
			'- \'\'',
			'...'
		],
		name => 'single_null'
	},
	{   out => '  ',
		in  => [
			'--- "  "',
			'...'
		],
		name => 'only_spaces'
	},
	{   out => [
			Any,
			{   'foo'  => 'bar',
				'this' => 'that'
			},
			'baz'
		],
		in => [
			'---',
			'- ~',
			'- foo: bar',
			'  this: that',
			'- baz',
			'...'
		],
		name => 'inline_nested_hash'
	},
	{   name => "Unprintables",
		in   => [
			"---",
			"- \"\\z\\x01\\x02\\x03\\x04\\x05\\x06\a\\x08\\t\\n\\v\\f\\r\\x0e\\x0f\"",
			"- \"\\x10\\x11\\x12\\x13\\x14\\x15\\x16\\x17\\x18\\x19\\x1a\\e\\x1c\\x1d\\x1e\\x1f\"",
			"- \" !\\\"#\$%&'()*+,-./\"",
			"- 0123456789:;<=>?",
			"- '\@ABCDEFGHIJKLMNO'",
			"- 'PQRSTUVWXYZ[\\]^_'",
			"- '`abcdefghijklmno'",
			"- 'pqrstuvwxyz\{|}~\x85'",
			"- \xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf",
			"- \xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf",
			"- \xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf",
			"- \xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf",
			"- \xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef",
			"- \xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff",
			"..."
		],
		out => [
			"\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\f\r\x0e\x0f",
			"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\e\x1c\x1d\x1e\x1f",
			" !\"#\$%&'()*+,-./",
			"0123456789:;<=>?",
			"\@ABCDEFGHIJKLMNO",
			"PQRSTUVWXYZ[\\]^_",
			"`abcdefghijklmno",
			"pqrstuvwxyz\{|}~\x85",
			"\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf",
			"\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf",
			"\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf",
			"\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf",
			"\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef",
			"\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff",
		],
	},
	{   name => 'Quoted hash keys',
		in   => [
			'---',
			'"quoted": Magic!',
			'"\n\t": newline, tab',
			'...',
		],
		out => {
			quoted => 'Magic!',
			"\n\t" => 'newline, tab',
		},
	},
	{   name   => 'Empty',
		in     => [],
		error => "Couldn't parse YAML",
	},
);

plan(@SCHEDULE * 1);

for (@SCHEDULE) -> %test {
	my $name = %test<name>;

	my $source = %test<in>.join("\n") ~ "\n";

	my %extra = (%test<schema>:exists) ?? (%test<schema>:kv) !! ();
	my $got = load-yaml($source, |%extra);
	my $want = %test<out>;

	if (%test<error> :exists) {
		ok(!$got, "$name: No result");
	}
	elsif (!$got.defined && $want.defined) {
		ok(False, "$name should parse") or note($got);
	}
	else {
#		unless ( ok !$?, "$name: No error" ) {
#		   diag "Error: $@\n";
#		}
		is-deeply($got, $want,   "$name: Result matches");
#		is($raw,        $source, "$name: Captured source matches");
	}
}
