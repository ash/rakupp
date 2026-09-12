# Regression: the async-socket and Channel bugs behind Log::Timeline's hang
# (2026-09-12). Its reactor is a `supply {}` holding `whenever $!events` over a
# Channel, and its test taps `$conn.Supply` twice; every one of those was wrong
# here, and the visible result was a stringified Channel arriving on the socket.
#
# Every expectation was checked against Rakudo 2026.08 via /opt/homebrew/bin/raku
# — NOT the bare name `raku`, which on the author's box is a symlink that has
# pointed at rakupp. This file is green on both engines.
#
# Ports are derived from the PID so two concurrent runs do not collide.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my $base = 20000 + ($*PID % 900) * 5;

# --- `whenever $channel` inside a `supply {}` block ------------------------
# It bound the block's parameter to the CHANNEL rather than to each value sent:
# one run, with the Channel itself. `react` was right all along, which is what
# made it look like a Channel bug rather than a supply-block one.
{
    my $c = Channel.new; my @got;
    my $t = (supply { whenever $c -> $v { @got.push($v.^name) } }).tap: -> $x { };
    $c.send("open-1"); sleep 0.4; $t.close;
    ck(@got, ["Str"], 'whenever $chan in a supply block sees the VALUE, open channel');
}
{
    my $c = Channel.new; $c.send("closed-1"); $c.close;
    my @got;
    my $t = (supply { whenever $c -> $v { @got.push($v.^name) } }).tap: -> $x { };
    sleep 0.4; $t.close;
    ck(@got, ["Str"], '…and with a closed, pre-filled one');
}
{   # the control: react was never broken, and must stay that way
    my $c = Channel.new; $c.send("react-1"); $c.close;
    my @got;
    react { whenever $c -> $v { @got.push($v.^name) } }
    ck(@got, ["Str"], 'whenever $chan in a react is unchanged');
}

# --- closing a TAP must not close the SOCKET ------------------------------
# The tap's closer used to shutdown(fd, SHUT_RD) so the blocked recv would
# return, which shut the read half down for everyone; the reader then closed
# the fd on its way out. A second tap on the same connection saw nothing.
sub with-echo(&body) {
    my $port = $base + 1;
    my $server = IO::Socket::Async.listen('localhost', $port);
    my $tap = $server.tap: -> $conn {
        $conn.Supply.lines.tap: -> $msg { $conn.print("R1\nR2\nR3\n") };
    };
    my $conn = await IO::Socket::Async.connect('localhost', $port);
    my $r = body($conn);
    $conn.close; $tap.close;
    $r
}
ck(with-echo(-> $conn {
        react { whenever $conn.Supply { }; whenever Promise.in(0.4) { done } }   # tap and drop
        await $conn.print("go\n");
        my @got;
        react {
            whenever $conn.Supply.lines { @got.push($_); done if @got == 3 }
            whenever Promise.in(4) { done }
        }
        @got
   }), ["R1", "R2", "R3"], 'a second tap on $conn.Supply still receives');

# --- a react CLOSES the tap it made, and an idle tap does not CONSUME -----
# Without this the reader worker outlived its react and went on reading the
# socket, delivering into a block that had already finished — so bytes meant
# for the next tap vanished. Log::Timeline lost the first of its queued events
# exactly this way.
{
    my $port = $base + 2;
    my $server = IO::Socket::Async.listen('localhost', $port);
    my $tap = $server.tap: -> $conn {
        $conn.Supply.lines.tap: -> $msg { $conn.print("LATE\n") };
    };
    my $conn = await IO::Socket::Async.connect('localhost', $port);
    my @seen;
    react { whenever $conn.Supply { @seen.push($_) }; whenever Promise.in(0.4) { done } }
    await $conn.print("go\n");     # the server answers AFTER the react has ended
    sleep 1.2;
    ck(@seen, [], 'a react\'s tap stops consuming once the react ends');

    my @after;                      # …and the bytes are still there for the next tap
    react {
        whenever $conn.Supply.lines { @after.push($_); done }
        whenever Promise.in(3) { done }
    }
    ck(@after, ["LATE"], '…so a later tap still sees what was written meanwhile');
    $conn.close; $tap.close;
}

# --- consecutive writes keep their order (control: this always worked) ----
{
    my $port = $base + 3;
    my $server = IO::Socket::Async.listen('localhost', $port);
    my $tap = $server.tap: -> $conn {
        $conn.Supply.lines.tap: -> $msg {
            $conn.print("A\n"); $conn.print("B\n"); $conn.print("C\n");
        };
    };
    my $conn = await IO::Socket::Async.connect('localhost', $port);
    await $conn.print("go\n");
    my @got;
    react {
        whenever $conn.Supply.lines { @got.push($_); done if @got == 3 }
        whenever Promise.in(4) { done }
    }
    ck(@got, ["A", "B", "C"], 'separate async writes arrive in order and none is lost');
    $conn.close; $tap.close;
}


# --- an OBJECT-KEYED hash hands `.keys` back as the OBJECT ----------------
# The payload is keyed by strings, so `.keys` used to answer the
# stringification and the original was gone: `.print(…) for %connections.keys`
# called .print on a stringified socket (Log::Timeline), and
# `keys.first(* eqv [1,2,3])` matched nothing (CBOR::Simple).
{
    my %h{Mu}; my $k = [7, 8, 9];
    %h{$k} = "sub-v";
    ck(%h.keys.head, [7, 8, 9], 'a subscript store keeps the key object');
    ck(%h{$k}, "sub-v", '…and still fetches by it');
}
{
    my %h{Mu}; my $k = [1, 2];
    %h.AT-KEY($k) = "atkey-v";                       # CBOR::Simple's spelling
    my $found = %h.keys.first(* eqv [1, 2]);
    ck(%h.AT-KEY($found), "atkey-v", 'an AT-KEY store round-trips through .keys');
}
{
    my %h{Mu}; my $k = [3, 4];
    %h.ASSIGN-KEY($k, "assign-v");
    ck(%h.keys.head, [3, 4], 'and so does ASSIGN-KEY');
}
{   # the control: a plain string-keyed hash is untouched by any of this
    my %p; %p<a> = 1; %p<b> = 2;
    ck(%p.keys.sort.List, ("a", "b"), 'a plain hash still answers Str keys');
}

# --- `done` inside a LAST phaser closes the react -------------------------
# It ran on the source's worker, where nothing had pushed the react, so `done`
# quietly did nothing and the react waited on its other sources.
{
    my $fired = False; my $timed = False;
    react {
        whenever Supply.from-list(1, 2) { LAST { $fired = True; done } }
        whenever Promise.in(3) { $timed = True; done }
    }
    ck(($fired, $timed), (True, False), 'done inside LAST ends the react');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
