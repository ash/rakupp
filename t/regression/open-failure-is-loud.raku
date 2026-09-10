# Issue #71: a write that cannot possibly land must say so at the open.
#
# The handle rakupp hands back carries no OS descriptor — every read and write
# reopens the path — so the open() builtin was the only place that could ever
# notice the path was unopenable, and it never looked. `open($p, :w)` on a
# missing directory answered a live handle, .print and .close both answered
# True, and the program learned about it at some unrelated slurp much later.
# (The reporter lost a temp directory mid-suite and every write into it
# "succeeded"; the first failure surfaced tests away from the cause.)
#
# Sibling of the same bug in spurt, fixed earlier: that one answered a quiet
# False. Both are Failures now.
#
# The exception IS an X::AdHoc, which is what a program can be written against:
# the docs name no type ("Fails with appropriate exception if the open fails"),
# Rakudo answers X::AdHoc, and `when X::AdHoc` is what code in the wild contains.
# So that is what these check — the SMARTMATCH, not the name.
#
# The name is X::IO::Open, and checking `.^name eq 'X::AdHoc'` is what this file
# used to do, because at the time an X:: name had no parents and the two were
# the same question. They are not any more: X::IO::Open IS-A X::AdHoc, every
# such CATCH fires, `.payload` is the message as Rakudo's is, and the name says
# which call failed where Rakudo's cannot — its message is "Failed to open file"
# for a slurp and a spurt alike.
# A directory is the one exception: X::IO::Directory is Rakudo's own type.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

my $gone = $*TMPDIR.add("open71-gone-$*PID");   # never created
my $f    = $gone.add('F');

# -- every write mode refuses, and names the real cause -----------------------
# (no `try` around the open: a Failure is a VALUE until it is used, and that is
#  the property the fix restores — `if $fh { … }` has to be able to see it)
for <w a rw update> -> $m {
    my $r = open($f.Str, |($m => True));
    check($r.^name, 'Failure', "open :$m into a missing directory answers a Failure");
    check($r.exception ~~ X::AdHoc, True, "…armed with an X::AdHoc, as Rakudo throws (:$m)");
    check($r.exception.^name, 'X::IO::Open', "…and the name says which call failed (:$m)");
    check($r.exception.payload.Str.contains('No such file'), True, "…with X::AdHoc's payload (:$m)");
    check($r.exception.Str.contains('No such file or directory'), True, "…and says why (:$m)");
    check($f.e, False, "…and created nothing (:$m)");
    check(?$r, False, "…and is false, so an `if open(...)` guard sees it (:$m)");
}

# -- the method form agrees (it delegates, so this guards the delegation) ------
{
    my $r = $f.open(:w);
    check($r.^name, 'Failure', '.IO.open(:w) agrees with open(:w)');
    check($r.exception ~~ X::AdHoc, True, '…same type');
    check($r.exception.^name, 'X::IO::Open', '…same name');
}

# -- read mode keeps the contract it already had ------------------------------
{
    try open($f.Str);
    check($! ~~ X::AdHoc, True, 'read-mode open on a missing file still refuses');
    check($!.^name, 'X::IO::Open', '…and names the call');
}

# -- a directory is a directory, in either direction --------------------------
for <w r> -> $m {
    my $r = open($*TMPDIR.Str, |($m => True));
    check($r.^name, 'Failure', "open of a directory (:$m) answers a Failure");
    check($r.exception.^name, 'X::IO::Directory', "…armed with X::IO::Directory (:$m)");
    check($r.exception.Str.contains('is a directory'), True, "…and says so (:$m)");
}

# -- spurt, the shape the issue was filed about -------------------------------
{
    my $r = $f.spurt('x');
    check($r.^name, 'Failure', 'spurt into a missing directory is a Failure, not False');
    check($r.exception.Str.contains('No such file or directory'), True, '…naming the missing directory');
}

# -- an open that CAN work is untouched ---------------------------------------
{
    my $ok = $*TMPDIR.add("open71-ok-$*PID.txt");
    $ok.unlink;
    my $h = open($ok.Str, :w);
    check($h.^name eq 'Failure', False, 'a writable path still opens');
    $h.print('hello');
    $h.close;
    check($ok.slurp, 'hello', '…and the bytes land');
    my $a = open($ok.Str, :a); $a.print('!'); $a.close;
    check($ok.slurp, 'hello!', ':a appends to what is there');
    $ok.unlink;
    my $fresh = $*TMPDIR.add("open71-fresh-$*PID.txt");
    $fresh.unlink;
    open($fresh.Str, :a).close;
    check($fresh.e, True, ':a on a missing file creates it');
    $fresh.unlink;
}

# -- a permission error is NOT reported as a missing file ---------------------
# Skipped on a host that cannot make a directory unwritable: running as root
# (where the write succeeds anyway), and --target=js, whose runtime has no
# .chmod — this file is part of the js corpus gate, where the interpreter is
# the oracle, so a probe the JS host cannot run must not fail the comparison.
{
    my $dir = $*TMPDIR.add("open71-ro-$*PID");
    $dir.mkdir;
    my $locked = ?(try { $dir.chmod(0o500); True });
    if $locked {
        my $inside = $dir.add('F');
        my $r = open($inside.Str, :w);
        if $r.^name eq 'Failure' {          # false when the process writes anyway
            check($r.exception ~~ X::AdHoc, True, 'an unwritable directory refuses too');
            check($r.exception.Str.contains('Permission denied'), True, '…it names the permission');
        }
        try $dir.chmod(0o700);
        $inside.unlink if $inside.e;
    }
    try $dir.rmdir;
}

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
