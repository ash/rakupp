# Issue #137: `"a {'it\'s'} z"` — an escaped quote in a string inside a `{ }`
# block of an interpolating string.
#
#   The block's end was found by a scan that took a backslash as an escape
#   only inside DOUBLE quotes, so the `\'` closed the single-quoted string,
#   the next `'` opened another, and the scan ran to the end of the string
#   looking for the `}`. The block's code then failed to parse, which was
#   swallowed: the block produced nothing and the rest of the string vanished,
#   without an error.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my $n = 3;
check "a {'it\'s'} z",                          "a it's z",  'an escaped quote';
check "a {$n == 1 ?? 'it\'s one' !! 'many'} z", 'a many z',  'in a branch not taken';
check "a {"it's"} z",                           "a it's z",  'the double-quoted spelling';
check "a {'\\'} z",                             'a \\ z',    'an escaped backslash';
check "a {'it\'s'} {'b}c'} z",                  "a it's b}c z", 'a brace in a later block';
check "a {"q\""} z",                            'a q" z',    'an escaped double quote';
check qq/a {'it\'s'} z/,                        "a it's z",  'qq//';
my $h = qq:to/END/;
    a {'it\'s'} z
    END
check $h,                                       "a it's z\n", 'a qq heredoc';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
