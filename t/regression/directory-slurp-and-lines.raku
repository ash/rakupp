# `.slurp` of a directory dies as Rakudo's IO::Path.slurp does, X::AdHoc
# "Tried to open directory <its absolute path>" (it was X::IO::Directory with
# another wording), and `.lines` of one is the `.open` it starts with,
# X::IO::Directory, where it answered no lines at all.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $d = $*TMPDIR.add("dir-slurp-{$*PID}");
$d.mkdir;
END { $d.rmdir if $d.e }

try $d.slurp;
ck $!.^name, 'X::AdHoc', '.slurp';
ck $!.message, "Tried to open directory {$d.absolute}", '…its message';
try slurp($d);
ck $!.message, "Tried to open directory {$d.absolute}", 'slurp()';
try $d.slurp(:bin);
ck $!.^name, 'X::AdHoc', '.slurp(:bin)';
try $d.lines.elems;
ck $!.^name, 'X::IO::Directory', '.lines';
ck $!.message, "'$d' is a directory, cannot do '.open' on a directory", '…its message';
try open($d);
ck $!.^name, 'X::IO::Directory', 'open (as before)';

say 'PASS';
