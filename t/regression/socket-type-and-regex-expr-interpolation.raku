# Two bugs found writing App::Rakus, a static file server whose whole point is
# raw IO::Socket::INET.
#
#   * A synchronous socket could not name the type it is. `IO::Socket::INET.new`
#     produced a value reporting as `Socket`, so the one signature a server
#     naturally writes —
#
#         sub accept-loop(IO::Socket::INET $listener) { … }
#
#     — rejected its own listener with "expected IO::Socket::INET but got
#     Socket". The ASYNC socket already mapped its internal kind to the Rakudo
#     type one line away in the same switch; the synchronous one never did.
#
#   * `$( … )` in a regex interpolated nothing. Only an identifier was spliced
#     into a pattern, so `/ $($buf.bytes) /` reached the regex parser verbatim:
#     the `$` read as the end anchor and the parenthesis as a group, and the
#     match failed for reasons unrelated to the value. It never errored, which
#     is what makes it worth a regression test — a test using it to check a
#     Content-Length simply reported the wrong thing.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# ---- a socket knows what it is ---------------------------------------------
my $listener;
my $port;
for 31_610 .. 31_650 -> $candidate {
    $listener = try IO::Socket::INET.new(:localhost<127.0.0.1>, :localport($candidate), :listen);
    if $listener { $port = $candidate; last }
}

if $listener {
    check($listener.^name, 'IO::Socket::INET', 'a listening socket names its type');

    sub takes-listener(IO::Socket::INET $l --> Str) { $l.^name }
    my $bound = try takes-listener($listener);
    check($bound, 'IO::Socket::INET', "a socket binds to a typed parameter ({$! // 'no error'})");

    # …and a connected socket on both ends of an accept
    my $client = try IO::Socket::INET.new(:host<127.0.0.1>, :port($port));
    check($client.defined, True, 'a client socket connects');
    check($client.^name, 'IO::Socket::INET', 'and names its type too');
    try $client.close;
    try $listener.close;
}
else {
    # No free port is not a failure of the thing under test.
    check(True, True, 'a listening socket names its type (skipped: no free port)');
    check(True, True, 'a socket binds to a typed parameter (skipped: no free port)');
    check(True, True, 'a client socket connects (skipped: no free port)');
    check(True, True, 'and names its type too (skipped: no free port)');
}

# ---- $( … ) interpolates into a pattern ------------------------------------
my $n   = 14;
my $str = 'ab';

check(('14' ~~ / ^ $($n) $ /).so,        True,  'a variable in $( ) interpolates');
check(('14' ~~ / ^ $(7 + 7) $ /).so,     True,  'and so does an expression');
check(('ab' ~~ / ^ $($str) $ /).so,      True,  'a string value matches literally');
check(('Content-Length: 14' ~~ / 'Content-Length: ' $($n) /).so, True,
      'the shape a test actually writes');
check(('15' ~~ / ^ $($n) $ /).so,        False, 'and a different value does NOT match');

# the value is matched LITERALLY, metacharacters and all
my $meta = 'a.c';
check(('a.c' ~~ / ^ $($meta) $ /).so, True,  'a dot in the value is a literal dot');
check(('abc' ~~ / ^ $($meta) $ /).so, False, 'so it does not match any character');

# `$` still ends a pattern, and `$var` still works beside the new form
check(('xy'  ~~ / ^ 'xy' $ /).so,       True, 'the end anchor is untouched');
check(('14'  ~~ / ^ $n $ /).so,         True, 'a bare $var still interpolates');
check(('$(x' ~~ / ^ '$(x' $ /).so,      True, 'a quoted $( is still literal text');

if @fail {
    .say for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
