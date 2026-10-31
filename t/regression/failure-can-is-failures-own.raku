# Regression: `.can` / `.^can` on a Failure answers Failure's own methods and
# Any's, not every name (issue #77, Red's "Dynamic variable $*RED-DB not
# found").
#
# A built-in value answers `.can` by a probe that calls the method and reads
# X::Method::NotFound. Any call on an unhandled Failure throws the exception
# it holds, which the probe took for "found", so `$failure.^can('anything')`
# was True. A missing dynamic variable is such a Failure, and Red's formatter
# writes
#
#     elsif try $*RED-DB.^can("table-name-formatter") {
#         $*RED-DB.table-name-formatter: $data
#     }
#
# which, with no $*RED-DB, took the branch and died there: eight of Red's
# test files, wherever a model's table name was asked before a database.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck so(try $*NO-SUCH-VARIABLE.^can('table-name-formatter')), False, 'a missing dynamic has no such method';
my $branch = do if False { 1 } elsif try $*NO-SUCH-VARIABLE.^can('x') { 2 } else { 3 };
ck $branch, 3, '…so Red\'s elsif falls through';

my $f = Failure.new('boom');
ck so($f.^can('foo')), False, 'a name Failure does not have';
ck so($f.can('foo')), False, '…through .can too';
for <exception handled defined Bool Str gist sink self mess backtrace Numeric Int raku list> -> $m {
    ck so($f.^can($m)), True, "Failure's own $m";
}
for <elems so not keys WHAT> -> $m {
    ck so($f.^can($m)), True, "Any's $m";
}
ck $f.handled, False, 'asking does not handle it';
my $msg = '';
try { $f.Int; CATCH { default { $msg = .message } } }
ck $msg, 'boom', '…and it still throws when used';

say 'PASS';
