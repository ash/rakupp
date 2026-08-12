# The Raku half of the byte-identical gate. grammar-smoke.raku CONCATENATES
# the shim (bindings/python/rakulang/grammar_shim.raku) in front of this file
# and runs the result under plain rakupp — the same source rk_eval feeds a
# host, so both sides exercise the identical shim, and this driver's output
# is the reference a host driver must reproduce byte for byte.
#
#   args: <grammar-file> <input-file>
#
# The output covers every G0 surface: compile with actions, whole-input
# parse, selective lazy access over every record (the two-fields-per-line
# shape from the plan's measurements), elems/islist, .made from same-file
# actions, the eager tree, a :rule subparse, and a failed parse.

my ($grammar-file, $input-file) = @*ARGS[0], @*ARGS[1];

sub canon($x) {
    given $x {
        when Positional  { '[' ~ $x.map({ canon($_) }).join(',') ~ ']' }
        when Associative { '{' ~ $x.keys.sort.map({ $_ ~ ':' ~ canon($x{$_}) }).join(',') ~ '}' }
        default          { ~$x }
    }
}

my $id = rk-grammar-compile($grammar-file.IO.slurp, 'Log', 'LogActions');
my $m  = rk-grammar-parse($id, $input-file.IO.slurp, '');
die "gate: the log corpus did not parse" unless $m.defined;

my $n = rk-match-walk($m, ['line'], 'elems');
say 'lines ' ~ $n;
say 'islist ' ~ rk-match-walk($m, ['line'], 'islist');

for ^$n -> $i {
    say rk-match-walk($m, ['line', $i, 'ip'], 'str')
        ~ ' ' ~ rk-match-walk($m, ['line', $i, 'status'], 'str');
}

say 'made ' ~ rk-match-walk($m, ['line', 0, 'size'], 'made');
say 'req.str ' ~ rk-match-walk($m, ['line', 42, 'req'], 'str');
say 'size.int ' ~ rk-match-walk($m, ['line', 42, 'size'], 'int');
say 'missing ' ~ rk-match-walk($m, ['nope'], 'bool');
say 'tree ' ~ canon(rk-match-walk($m, ['line', 999], 'tree'));

my $one = rk-grammar-parse($id, '7.7.7.7 - - [x] "GET / HTTP/1.1" 200 5' ~ "\n", 'line');
say 'rule-parse ' ~ rk-match-walk($one, ['status'], 'str');

my $bad = rk-grammar-parse($id, 'this is not a log line', '');
say 'failed-parse ' ~ ($bad.defined ?? 'Match' !! 'None');

my $d = rk-grammar-diagnosis('this is not a log line');
say 'diag ' ~ ($d.defined ?? 'line ' ~ $d<line> ~ ' col ' ~ $d<col> ~ ' rule ' ~ $d<rule> !! 'none');
