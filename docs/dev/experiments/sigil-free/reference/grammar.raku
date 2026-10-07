grammar KV {
    token TOP   { <pair>+ % \s+ }
    token pair  { <key> '=' <value> }
    token key   { \w+ }
    token value { \w+ }
}
my $key = 'unused';
my $m = KV.parse('a=1 b=2 c=3');
my %table = $m<pair>.map({ ~.<key> => ~.<value> }).Hash;
say %table.sort.map(*.kv.join('=')).join(' ');
say $key;
