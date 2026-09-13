# Regression: `IO::Handle.native-descriptor` — the OS file descriptor behind a
# handle, and the first thing every terminal module asks for. Terminal-API
# reads the tty state with `Terminal::API::get-config($*IN.native-descriptor)`,
# and Terminal::LineEditor waits on it; neither could start without the method.
#
# The STANDARD handles have real descriptors and are asserted exactly. A handle
# opened on a path is only asserted to answer an Int, because the two engines
# genuinely differ there: Rakudo keeps an open fd and reports it, while this
# engine's IO layer is path-and-buffer based and holds none, so it answers C's
# -1. That difference is deliberate and recorded in MODULE-FINDINGS.md.
#
# The row that matters most is the last one: answering Nil looked harmless and
# made `Nil >= 0` TRUE, which is how Roast's own S32-io/native-descriptor.t
# came to report 4 of 4 while one of its rows was asking a question this engine
# cannot answer.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck $*IN.native-descriptor,  0, '$*IN  is descriptor 0';
ck $*OUT.native-descriptor, 1, '$*OUT is descriptor 1';
ck $*ERR.native-descriptor, 2, '$*ERR is descriptor 2';

my $path = $*TMPDIR.add("rakupp-nd-{$*PID}.txt");
given open($path, :w) {
    my $fd = .native-descriptor;
    ck ($fd ~~ Int), True, 'a file handle answers an Int';
    # …and whatever it answers, it must not be an undefined value dressed up as
    # a number: `Nil >= 0` is True, and that is the false pass this pins against
    ck $fd.defined, True, 'a file handle does not answer an undefined value';
    .close;
}
unlink $path;

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
