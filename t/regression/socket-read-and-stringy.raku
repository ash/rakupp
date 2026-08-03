# Regression: the four fixes that took LWP::Simple from 7 to 18 of its own 18
# test files, 2026-08-04. Nothing here is about HTTP — LWP is just the first
# code in the battery to bind a listener by name and read a fixed byte count.
# Every expectation was checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. `.Stringy` is Mu's string coercion — `self.Str` — and did not exist, so it
#    died on every type, including the enum values LWP builds its request line
#    from. Forwarded like `.perl` forwards to `.raku`, with the same escape for a
#    class that defines its own.
{
    my enum RT <GET POST>;
    ck(RT::GET.Stringy, 'GET', 'an enum value stringifies');
    ck((42.Stringy, '5'.Stringy, 1.5.Stringy), ('42', '5', '1.5'), 'the basic types');
    ck((1, 2).Stringy, '1 2', 'and a list');

    my class WithStr    { method Str { 'from-Str' } }
    my class WithOwn    { method Stringy { 'own' }; method Str { 'not-this' } }
    ck(WithStr.new.Stringy, 'from-Str', 'a user .Str is what .Stringy reaches');
    ck(WithOwn.new.Stringy, 'own',      'a user .Stringy wins outright');
    ck((try Buf.new(1).Stringy).defined, False, 'a Buf refuses, exactly as .Str does');
}

# 2. A listener binds by NAME as well as by address. Only the client path
#    resolved, so `:localhost<localhost>` bound to INADDR_NONE and `.new`
#    answered Nil with nothing to say why.
# 3. `.localport` / `.localhost` — the port is only knowable after bind, which is
#    the whole point of asking for `:localport(0)`, and the host answers the name
#    as GIVEN rather than what it resolved to.
{
    my $srv = IO::Socket::INET.new(:listen, :localhost<localhost>, :localport(0));
    ck($srv.defined, True, 'a listener binds by hostname');
    ck($srv.localport > 0, True, 'port 0 gets a real port from the OS');
    ck($srv.localhost, 'localhost', '.localhost answers the name as given');
    $srv.close;
}

# 4. `.read($n)` answers EXACTLY $n bytes, blocking until it has them; `.recv($n)`
#    answers at most $n. One recv() served both, so a reader asking for a fixed
#    count got whatever was in the first packet — silent until a message straddles
#    a packet boundary, and then a chunked HTTP body cannot parse its own header.
{
    my $srv = IO::Socket::INET.new(:listen, :localhost<127.0.0.1>, :localport(0));
    my $port = $srv.localport;
    start {
        my $c = $srv.accept;
        $c.write(Buf.new(65, 66, 67, 68));              # 4 bytes…
        sleep 0.3;
        $c.write(Buf.new(69, 70, 71, 72, 73, 74));      # …then 6 more
        sleep 0.3;
        $c.close;
    }
    my $cl = IO::Socket::INET.new(:host<127.0.0.1>, :port($port));
    my $got = $cl.read(10);
    ck($got.elems, 10, '.read waits for the full count across packets');
    ck($got.decode('latin-1'), 'ABCDEFGHIJ', 'and the bytes are in order');
    $cl.close;
    $srv.close;
}

# 5. A statement prefix takes the WHOLE remaining expression — through `=`,
#    through the comma list, and through the loose `and`/`or`. Stopping at the
#    assignment level made `try EXPR or die MSG` parse as `(try EXPR) or die MSG`,
#    so the die escaped the very `try` it was written inside. HTTP::Tiny
#    validates an absent proxy with exactly that idiom and died on construction.
{
    my sub bad() { die 'inner' }

    my $reached = False;
    try bad() or $reached = True;
    ck($reached, False, 'try swallows a trailing `or`');

    ck((do 0 or 5), 5, 'and so does do');
    ck((do 1, 2).List, (1, 2), 'a statement prefix takes the comma list');
    ck([try bad(), 2].elems, 1, 'so the whole thing is ONE element');

    # the assignment form still binds the way it did
    my $v = try bad();
    ck($v.defined, False, 'try of a dying call is undefined');
    my $w = try 40 + 2;
    ck($w, 42, 'and yields the value when nothing dies');
}

say $ok ?? 'PASS' !! 'FAIL';
