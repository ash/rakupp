# Regression: an object hash copied into another object hash keeps its keys
# as the objects they were (issue #77, FCO's item 3).
#
# `my %copy{HashKey} = %original` handed back "(HashKey)", a Str, for the
# key: the copy took the entries but not ValueHash's side table of the
# objects they were subscripted with, so `.keys` fell back to the index
# string. Red keys query branches by AST objects and lost them this way.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class HashKey { }

my Int %original{HashKey};
%original{HashKey} = 42;

my Int %copy{HashKey} = %original;
ck %copy.keys[0].^name, 'HashKey', 'a typed copy keeps the type-object key';
ck %copy{HashKey}, 42, '…and finds its value under it';

my %untyped{HashKey} = %original;
ck %untyped.keys[0].^name, 'HashKey', 'an untyped object-hash copy too';

my %later{HashKey};
%later = %original;
ck %later.keys[0].^name, 'HashKey', 'an assignment after the declaration too';

my $k = HashKey.new;
my %inst{HashKey};
%inst{$k} = 1;
my %inst-copy{HashKey} = %inst;
ck %inst-copy.keys[0] === $k, True, 'an instance key stays the same object';

my %ints{Mu};
%ints{1} = 'one';
my %ints-copy{Mu} = %ints;
ck %ints-copy.keys[0].^name, 'Int', 'an Int key stays an Int';
%ints-copy{1}:delete;
ck %ints-copy.elems, 0, 'deleting from the copy…';
ck %ints.keys[0].^name, 'Int', '…leaves the original and its key';

# a PLAIN hash still stringifies, as in Rakudo
my %plain = %ints;
ck %plain.keys[0].^name, 'Str', 'a plain hash takes the keys as Strs';

say 'PASS';
