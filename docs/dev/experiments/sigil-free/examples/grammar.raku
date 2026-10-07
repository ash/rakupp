# A grammar: its token bodies are regexes and stay untouched; the actions
# and the code around them are sigil-free.
grammar KV {
    token TOP   { <pair>+ % \s+ }
    token pair  { <key> '=' <value> }
    token key   { \w+ }
    token value { \w+ }
}
my key = 'unused';
my parsed = KV.parse('a=1 b=2 c=3');
my table = parsed<pair>.map({ ~.<key> => ~.<value> }).Hash;
say table.sort.map(*.kv.join('=')).join(' ');
say key;
