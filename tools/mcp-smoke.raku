# The MCP gate: does `rakupp --mcp` still speak the protocol, and do the two
# tools still answer what the guide (docs/guide/MCP.md) says they answer?
#
# The server is driven exactly as an MCP client drives it — JSON-RPC 2.0, one
# object per line, over the child's stdio: a batch of requests goes down its
# stdin, stdin closes (how a client ends a server), and every response line
# comes back for inspection. The checks are substring pins on those lines, on
# the same principle as the bindings gate's byte-compares: what the guide
# prints is what the server must say.
#
# What is pinned, in order: the initialize handshake, both tools listed, the
# SESSION (state carried across raku calls — the whole point of the
# server), output capture, big-integer and exact-Rat arithmetic, a die
# crossing as isError WITHOUT killing the session, the match tree of a
# grammar parse, an actions class's .made, the line/column/rule diagnosis of
# a failed parse, and the two protocol error codes. A second server run pins
# the watchdog: an uninterruptible `loop {}` must be ANSWERED (isError) and
# the process must exit, because a hung tool call wedges the client's agent.
#
# Run:  build/rakupp tools/mcp-smoke.raku [build-dir]   (default: build)

my $ROOT   = $?FILE.IO.parent.parent;
my @args   = @*ARGS;
my $BUILD  = $ROOT.add(@args[0] // 'build');
my $rakupp = $BUILD.add('rakupp' ~ ($*KERNEL.name eq 'win32' ?? '.exe' !! ''));
my $errors = 0;

sub check(Bool $ok, $desc, $detail = '') {
    if $ok {
        say "ok - $desc";
    }
    else {
        $errors++;
        say "NOT OK - $desc";
        say $detail.indent(4) if $detail;
    }
}

unless $rakupp.e {
    say "skip - $rakupp is not built";
    say "mcp-smoke: ok (nothing to check)";
    exit 0;
}

# ---- the main conversation --------------------------------------------------
# Requests are Q-literals: what stands here is byte-for-byte what goes down
# the pipe, escapes included.

my @requests =
    Q<{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"mcp-smoke","version":"0"}}}>,
    Q<{"jsonrpc":"2.0","method":"notifications/initialized"}>,
    Q<{"jsonrpc":"2.0","id":2,"method":"tools/list"}>,
    Q<{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"raku","arguments":{"code":"my $x = 41"}}}>,
    Q<{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"raku","arguments":{"code":"$x + 1"}}}>,
    Q<{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"raku","arguments":{"code":"say 6 * 7"}}}>,
    Q<{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"raku","arguments":{"code":"[*] 1..25"}}}>,
    Q<{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"raku","arguments":{"code":"0.1 + 0.2 == 0.3"}}}>,
    Q<{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"raku","arguments":{"code":"die 'boom'"}}}>,
    Q<{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"raku","arguments":{"code":"$x"}}}>,
    Q<{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"raku-parse","arguments":{"grammar":"grammar G { token TOP { (\\d+) '-' (\\d+) } }","text":"12-34"}}}>,
    Q<{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"raku-parse","arguments":{"grammar":"grammar Sum { token TOP { <a> '+' <b> } token a { \\d+ } token b { \\d+ } }; class SumA { method TOP($/) { make $<a>.Int + $<b>.Int } }","text":"2+40","name":"Sum","actions":"SumA"}}}>,
    Q<{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"raku-parse","arguments":{"grammar":"grammar Line { token TOP { <word> ' ' <num> } token word { \\w+ } token num { \\d+ } }","text":"hello world","name":"Line"}}}>,
    Q<{"jsonrpc":"2.0","id":13,"method":"nonsense/method"}>,
    Q<{"jsonrpc":"2.0","id":14,"method":"tools/call","params":{"name":"raku_nope","arguments":{}}}>,
    Q<{"jsonrpc":"2.0","id":15,"method":"tools/call","params":{"name":"raku","arguments":{"code":"exit 9"}}}>,
    Q<{"jsonrpc":"2.0","id":16,"method":"tools/call","params":{"name":"raku","arguments":{"code":"$x"}}}>,
    ;

my $p = run $rakupp.Str, '--mcp', '--timeout=120', :in, :out, :err;
$p.in.print(@requests.join("\n") ~ "\n");
$p.in.close;
my @lines = $p.out.slurp(:close).lines;
$p.err.slurp(:close);

check $p.exitcode == 0, 'the server exits 0 when its client closes stdin',
    "exit code {$p.exitcode}";

# One request carried no id (the initialized notification): 17 in, 16 answers.
check @lines.elems == 16, 'every id answered, the notification not',
    "got {@lines.elems} lines";
check @lines.grep(!*.starts-with('{')) == 0,
    'stdout carries the protocol and nothing else';

my sub reply(Int $n) { @lines.first(*.contains(Q{"id":} ~ $n ~ ',')) // '' }

check reply(1).contains(Q{"serverInfo"}) && reply(1).contains(Q{"name":"rakupp"}),
    'initialize answers with serverInfo', reply(1);
check reply(1).contains(Q{"protocolVersion":"2025-06-18"}),
    'the client protocol revision is echoed back', reply(1);
check reply(2).contains(Q{"name":"raku","}) && reply(2).contains(Q{"name":"raku-parse"}),
    'tools/list lists both tools, raku and raku-parse', reply(2);
check reply(3).contains(Q{=> 41}), 'the raku tool returns the value as => gist', reply(3);
check reply(4).contains(Q{=> 42}), 'the session KEEPS state: $x from the previous call', reply(4);
check reply(5).contains(Q{"text":"42\n"}), 'printed output is captured, value suppressed', reply(5);
check reply(6).contains(Q{=> 15511210043330985984000000}), '25! — integers do not overflow', reply(6);
check reply(7).contains(Q{=> True}), '0.1 + 0.2 == 0.3 — decimals are exact Rats', reply(7);
check reply(8).contains(Q{"isError":true}) && reply(8).contains('boom'),
    'a die crosses as isError with its message', reply(8);
check reply(9).contains(Q{=> 41}), 'the session SURVIVES the die', reply(9);
# A tool's text is a JSON string INSIDE the envelope, so the pins carry the
# extra escaping the wire carries: \" for every quote of the payload.
check reply(10).contains(Q{\"matched\":true}) && reply(10).contains(Q{\"0\":\"12\"}) && reply(10).contains(Q{\"1\":\"34\"}),
    'raku-parse: positional captures in the tree', reply(10);
check reply(11).contains(Q{\"made\":42}) && reply(11).contains(Q{\"a\":\"2\"}),
    'raku-parse: named captures, and the actions class made 42', reply(11);
check reply(12).contains(Q{\"matched\":false}) && reply(12).contains(Q{\"rule\":\"num\"})
        && reply(12).contains(Q{\"column\":7}),
    'a failed parse is diagnosed: line, column, deepest rule', reply(12);
check reply(13).contains('-32601'), 'an unknown method is a JSON-RPC error', reply(13);
check reply(14).contains('-32602') && reply(14).contains('raku_nope'),
    'an unknown tool names itself in the error', reply(14);
check reply(15).contains(Q{"isError":true}) && reply(15).contains(Q{exit(9)}),
    'exit comes back as an error naming its code — the server lives', reply(15);
check reply(16).contains(Q{=> 41}), 'the session SURVIVES the exit attempt', reply(16);

# ---- the watchdog -----------------------------------------------------------
# `loop {}` cannot be interrupted, so the contract is: answer the request
# (isError), then EXIT so the client can restart a fresh session. Both halves
# checked; a wedged server here would hang this gate, which is itself the test.

my @hang =
    Q<{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"raku","arguments":{"code":"my $a = 5"}}}>,
    Q<{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"raku","arguments":{"code":"loop { }"}}}>,
    ;
my $w = run $rakupp.Str, '--mcp', '--timeout=2', :in, :out, :err;
$w.in.print(@hang.join("\n") ~ "\n");
$w.in.close;
my @wlines = $w.out.slurp(:close).lines;
$w.err.slurp(:close);
my $hung = @wlines.first(*.contains(Q{"id":2,})) // '';
check $hung.contains(Q{"isError":true}) && $hung.contains('ran longer than'),
    'the watchdog answers a stuck call before exiting', $hung;

# ---- the GIL leg ------------------------------------------------------------
# RAKUPP_GIL=1 selects the cooperative GIL (the parallel-bisection leg). Under
# own_stack every eval runs on its own short-lived thread, and a mutex belongs
# to the thread that locked it — so GIL ownership must ride the entry hop the
# way the execution registers do. start/await in one call, more calls after,
# is exactly the pattern that once left the lock with a dead thread.

%*ENV<RAKUPP_GIL> = '1';
my @gil =
    Q<{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"raku","arguments":{"code":"my $p = start { 42 }; await $p"}}}>,
    Q<{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"raku","arguments":{"code":"my $q = start { 7 }; await $q"}}}>,
    Q<{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"raku","arguments":{"code":"2 + 2"}}}>,
    ;
my $g = run $rakupp.Str, '--mcp', '--timeout=60', :in, :out, :err;
$g.in.print(@gil.join("\n") ~ "\n");
$g.in.close;
my @glines = $g.out.slurp(:close).lines;
$g.err.slurp(:close);
%*ENV<RAKUPP_GIL>:delete;
check $g.exitcode == 0, 'GIL leg: the server exits 0 when its client closes stdin',
    "exit code {$g.exitcode}";
check (@glines.first(*.contains(Q{"id":1,})) // '').contains(Q{=> 42})
        && (@glines.first(*.contains(Q{"id":2,})) // '').contains(Q{=> 7}),
    'GIL leg: start/await answers across the eval hop';
check (@glines.first(*.contains(Q{"id":3,})) // '').contains(Q{=> 4}),
    'GIL leg: the session runs on after its workers';

if $errors {
    say "mcp-smoke: $errors problem{$errors == 1 ?? '' !! 's'}";
    exit 1;
}
say "mcp-smoke: ok";
