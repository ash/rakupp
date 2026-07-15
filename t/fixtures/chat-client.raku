#!/usr/bin/env raku
# A two-client checker for the chat showcase, run as its OWN process by t/run.raku
# (rakupp is flaky holding two live client sockets in the same process that also
# shell-launched the server, but a separate client process is fine). It connects
# alice and bob, exercises join/broadcast/who, and prints CHAT-OK or CHAT-FAIL.
#
#   rakupp t/fixtures/chat-client.raku <port>

sub MAIN(Int $port) {
    my @fail;
    sub conn() { IO::Socket::INET.new(:host('127.0.0.1'), :port($port)) }
    sub rd($s) { ($s.recv // '') }

    my $a = conn();
    rd($a);                                 # welcome / nick prompt
    $a.print("alice\n");
    @fail.push("nick") unless rd($a).contains('alice');

    my $b = conn();
    rd($b); $b.print("bob\n"); rd($b);

    my $join = rd($a);                      # alice should see bob join
    @fail.push("join") unless $join.contains('bob') && $join.contains('joined');

    $b.print("hello everyone\n");
    @fail.push("broadcast") unless rd($a).contains('bob: hello everyone');

    $a.print("/who\n");
    my $who = rd($a);
    @fail.push("who") unless $who.contains('alice') && $who.contains('bob');

    $a.close; $b.close;

    if @fail { say "CHAT-FAIL: {@fail.join(',')}"; exit 1 }
    else     { say "CHAT-OK"; exit 0 }
}
