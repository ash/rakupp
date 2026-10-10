# Regression: nqp::nextfiledir handed out `.` and `..` first and answered Nil at
# the end of the directory. paths reads it as `my str $entry = nqp::nextfiledir(
# $!handle)` until the entry is empty, and since a native str refuses Nil
# (a7dcefa7, 2026-09-23, as Rakudo does) every walk died with "Cannot unbox a
# type object (Nil) to a str." — paths 10.3 self-failed, and rak, App::Rak and
# everything else that walks a tree with it went down too. As MoarVM 2026.09
# does, `.` and `..` are never handed out and the end is the empty string.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $dir = $*TMPDIR.add("rakupp-nextfiledir-$*PID");
$dir.mkdir;
$dir.add('a').mkdir;
$dir.add('b.txt').spurt('x');
LEAVE { try $dir.add('b.txt').unlink; try $dir.add('a').rmdir; try $dir.rmdir }

# the raw op: two names, then the empty string
{
    my $d := nqp::opendir($dir.Str);
    my @seen;
    loop {
        my $e := nqp::nextfiledir($d);
        last if @seen > 10;
        @seen.push($e);
        last unless $e.defined && $e ne '';
    }
    nqp::closedir($d);
    ck(@seen.elems, 3, 'two entries and the end');
    ck(@seen.head(2).sort.List, ('a', 'b.txt'), 'no . and ..');
    ck(@seen.tail, '', 'the end of the directory is the empty string');
}

# paths' own shape: the entry in a native str, read until it is empty
{
    my $handle := nqp::opendir($dir.Str);
    sub entry(--> str) {
        nqp::until(
          nqp::isnull($handle)
            || nqp::isnull_s(my str $entry = nqp::nextfiledir($handle))
            || (nqp::isne_s($entry, '.') && nqp::isne_s($entry, '..')),
          nqp::null
        );
        nqp::if(nqp::isnull_s($entry), '', $entry)
    }
    my @names;
    while nqp::chars(my str $e = entry()) {
        @names.push($e);
        last if @names > 10;
    }
    nqp::closedir($handle);
    ck(@names.sort.List, ('a', 'b.txt'), 'a native-str walk ends without dying');
}

# an empty directory ends at once
{
    my $d := nqp::opendir($dir.add('a').Str);
    my str $e = nqp::nextfiledir($d);
    nqp::closedir($d);
    ck($e, '', 'an empty directory has nothing to hand out');
}

say $fails ?? "FAILED $fails" !! "PASS";
