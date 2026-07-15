#!/usr/bin/env raku
# A tiny in-memory key–value server with a Redis-flavoured text protocol, over
# raw TCP. Like the chat server it is concurrent — one `start` thread per
# connection — but instead of broadcasting, the connections share a single
# mutable store guarded by a `Lock`, and each command gets a reply. It's the
# "network protocol" showcase: no HTTP, just a line protocol you can drive by
# hand with `nc`.
#
#   build/rakupp showcase/kvstore/kvstore.raku            # listens on 127.0.0.1:6380
#   build/rakupp --exe -o kvstore showcase/kvstore/kvstore.raku && ./kvstore
#   PORT=7000 build/rakupp showcase/kvstore/kvstore.raku
#
# Talk to it: `nc 127.0.0.1 6380`, then one command per line —
#   SET name ada        -> OK
#   GET name            -> ada
#   INCR hits           -> 1
#   APPEND name " lovelace" -> 12
#   KEYS                -> name hits
#   DEL name            -> 1
#   HELP                -> the command list

constant HOST = '0.0.0.0';

# ---------- shared store (guarded by $lock) ----------------------------
my $lock  = Lock.new;
my %store;

# Split a command line into a verb and its arguments, honouring "double quotes"
# so a value can contain spaces: SET greeting "hello world". A hand scanner
# rather than a regex — literal quote marks are awkward to embed in a regex.
sub tokenize(Str $line --> List) {
    my @toks;
    my @c = $line.comb;
    my $i = 0;
    while $i < @c.elems {
        my $ch = @c[$i];
        if $ch eq ' ' || $ch eq "\t" { $i++; next }
        my $s = '';
        if $ch eq '"' {                            # quoted run, spaces allowed inside
            $i++;
            while $i < @c.elems && @c[$i] ne '"' { $s ~= @c[$i]; $i++ }
            $i++;                                  # skip closing quote
        }
        else {                                     # bare run up to whitespace
            while $i < @c.elems && @c[$i] ne ' ' && @c[$i] ne "\t" { $s ~= @c[$i]; $i++ }
        }
        @toks.push($s);
    }
    @toks.List;
}

# ---------- command handlers -------------------------------------------
# Each returns the reply string (without the trailing newline).
sub handle(@t --> Str) {
    return '' unless @t;
    my $cmd = @t[0].uc;
    my @a   = @t[1..*];
    given $cmd {
        when 'SET' {
            return "ERR usage: SET key value" unless @a >= 2;
            $lock.protect: { %store{@a[0]} = @a[1..*].join(' ') };
            "OK";
        }
        when 'GET' {
            return "ERR usage: GET key" unless @a == 1;
            my $v = $lock.protect: { %store{@a[0]} };
            $v.defined ?? $v !! "(nil)";
        }
        when 'DEL' {
            my $n = 0;
            $lock.protect: { for @a -> $k { $n++ if %store{$k}:delete } };
            ~$n;
        }
        when 'EXISTS' {
            my $r = $lock.protect: { (%store{@a[0]}:exists) ?? 1 !! 0 };
            ~$r;
        }
        when 'INCR' | 'DECR' {
            return "ERR usage: $cmd key" unless @a == 1;
            my $step = $cmd eq 'INCR' ?? 1 !! -1;
            my $out;
            $lock.protect: {
                my $cur = %store{@a[0]} // '0';
                if $cur !~~ /^ '-'? \d+ $/ { $out = "ERR value is not an integer" }
                else { my $new = $cur.Int + $step; %store{@a[0]} = ~$new; $out = ~$new }
            };
            $out;
        }
        when 'APPEND' {
            return "ERR usage: APPEND key value" unless @a >= 2;
            my $len;
            $lock.protect: {
                %store{@a[0]} = (%store{@a[0]} // '') ~ @a[1..*].join(' ');
                $len = %store{@a[0]}.chars;
            };
            ~$len;
        }
        when 'KEYS' {
            my @k = $lock.protect: { %store.keys.sort.List };
            @k ?? @k.join(' ') !! "(empty)";
        }
        when 'DBSIZE' { ~($lock.protect: { %store.elems }) }
        when 'FLUSHALL' { $lock.protect: { %store = () }; "OK" }
        when 'PING' { @a ?? @a.join(' ') !! "PONG" }
        when 'HELP' {
            "commands: SET GET DEL EXISTS INCR DECR APPEND KEYS DBSIZE FLUSHALL PING QUIT";
        }
        default { "ERR unknown command '$cmd' (try HELP)" }
    }
}

# ---------- per-connection handler (its own thread) --------------------
sub serve($sock) {
    sub put(Str $s --> Bool) { (try { $sock.print($s); True }) // False }
    return unless put("rakupp-kv ready. One command per line; HELP for the list.\r\n");
    LINES: loop {
        my $raw = $sock.recv;
        last unless $raw.defined && $raw ne '';
        for $raw.lines -> $line {
            my @t = tokenize($line.trim);
            next unless @t;
            last LINES if @t[0].uc eq 'QUIT';
            last LINES unless put(handle(@t) ~ "\r\n");
        }
    }
    try { $sock.close; }
}

# ---------- accept loop -------------------------------------------------
sub MAIN($port-arg?) {
    my $port = ($port-arg // %*ENV<PORT> // 6380).Int;
    my $listener = IO::Socket::INET.new(:localhost(HOST), :localport($port), :listen);
    note "rakupp-kv listening on 127.0.0.1:$port  (Ctrl-C to stop)";
    loop {
        my $conn = $listener.accept;
        start serve($conn);
    }
}
