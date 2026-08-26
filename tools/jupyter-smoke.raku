# The Jupyter gate: does `rakupp --jupyter` still speak the protocol a notebook
# speaks, all the way down to the wire?
#
# There is no ZeroMQ here either. This gate is a small Jupyter CLIENT written
# in Raku: it opens the five TCP sockets itself, performs the ZMTP greeting and
# the NULL handshake as a DEALER (shell, control), a SUB (iopub) and a REQ
# (heartbeat), signs every message with its OWN HMAC-SHA256 — implemented from
# scratch below and pinned against the RFC 4231 vectors — and reads the
# kernel's frames back. So the two independent implementations of both halves
# have to agree: a signature the kernel computes differently is a message the
# kernel drops, and this gate would go red.
#
# What is pinned, in order: the RFC 4231 vectors; kernel_info; a cell that
# prints AND has a value (stream + execute_result, the REPL's rule); the
# SESSION carried between cells (the whole point of a kernel); exact rational
# arithmetic, because that is the engine's own claim; a die crossing as an
# error message WITHOUT killing the kernel; jupyter-display's rich output; a
# forged signature being ignored; the heartbeat echo; and shutdown_request
# actually shutting the kernel down.
#
# Run:  build/rakupp tools/jupyter-smoke.raku [build-dir]   (default: build)

my $ROOT   = $?FILE.IO.parent.parent;
my @args   = @*ARGS;
my $BUILD  = $ROOT.add(@args[0] // 'build');
my $rakupp = $BUILD.add('rakupp' ~ ($*KERNEL.name eq 'win32' ?? '.exe' !! ''));
my $errors = 0;

sub check(Bool(Any) $ok, $desc, $detail = '') {
    LEAVE $*OUT.flush;   # a gate that hangs must still show how far it got
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
    say "jupyter-smoke: ok (nothing to check)";
    exit 0;
}

# ---- SHA-256 and HMAC, the client's own -------------------------------------

constant @K =
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2;

sub rotr($x, $n) { (($x +> $n) +| ($x +< (32 - $n))) +& 0xFFFFFFFF }

sub sha256(Buf $msg --> Buf) {
    my @h = 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19;
    my @m = $msg.list;
    my $bits = @m.elems * 8;
    @m.push(0x80);
    @m.push(0) while @m.elems % 64 != 56;
    for (0..7).reverse -> $i { @m.push(($bits +> (8 * $i)) +& 0xFF) }

    loop (my $off = 0; $off < @m.elems; $off += 64) {
        my @w;
        for ^16 -> $i {
            @w.push((@m[$off + 4*$i] +< 24) +| (@m[$off + 4*$i + 1] +< 16)
                  +| (@m[$off + 4*$i + 2] +< 8) +| @m[$off + 4*$i + 3]);
        }
        for 16..63 -> $i {
            my $s0 = rotr(@w[$i-15], 7) +^ rotr(@w[$i-15], 18) +^ (@w[$i-15] +> 3);
            my $s1 = rotr(@w[$i-2], 17) +^ rotr(@w[$i-2], 19) +^ (@w[$i-2] +> 10);
            @w.push((@w[$i-16] + $s0 + @w[$i-7] + $s1) +& 0xFFFFFFFF);
        }
        my ($a, $b, $c, $d, $e, $f, $g, $hh) = @h;
        for ^64 -> $i {
            my $S1 = rotr($e, 6) +^ rotr($e, 11) +^ rotr($e, 25);
            my $ch = ($e +& $f) +^ ((0xFFFFFFFF +^ $e) +& $g);
            my $t1 = ($hh + $S1 + $ch + @K[$i] + @w[$i]) +& 0xFFFFFFFF;
            my $S0 = rotr($a, 2) +^ rotr($a, 13) +^ rotr($a, 22);
            my $maj = ($a +& $b) +^ ($a +& $c) +^ ($b +& $c);
            my $t2 = ($S0 + $maj) +& 0xFFFFFFFF;
            ($hh, $g, $f, $e) = $g, $f, $e, ($d + $t1) +& 0xFFFFFFFF;
            ($d, $c, $b, $a) = $c, $b, $a, ($t1 + $t2) +& 0xFFFFFFFF;
        }
        my @add = $a, $b, $c, $d, $e, $f, $g, $hh;
        for ^8 -> $i { @h[$i] = (@h[$i] + @add[$i]) +& 0xFFFFFFFF }
    }
    Buf.new(@h.map({ ($_ +> 24) +& 0xFF, ($_ +> 16) +& 0xFF, ($_ +> 8) +& 0xFF, $_ +& 0xFF }).flat);
}

sub hmac-sha256(Buf $key, Buf $msg --> Buf) {
    my @k = $key.elems > 64 ?? sha256($key).list !! $key.list;
    @k.push(0) while @k.elems < 64;
    my $inner = sha256(Buf.new(@k.map(* +^ 0x36), |$msg.list));
    sha256(Buf.new(@k.map(* +^ 0x5c), |$inner.list));
}

sub hex(Buf $b) { $b.list.map({ .fmt('%02x') }).join }

# RFC 4231, cases 1 and 2 — if these hold, an agreeing kernel is right too.
check hex(sha256('abc'.encode('utf8'))) eq
    'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
    'SHA-256 matches the FIPS 180-4 "abc" vector';
check hex(hmac-sha256(Buf.new(0x0b xx 20), 'Hi There'.encode('utf8'))) eq
    'b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7',
    'HMAC-SHA256 matches RFC 4231 case 1';
check hex(hmac-sha256('Jefe'.encode('utf8'), 'what do ya want for nothing?'.encode('utf8'))) eq
    '5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843',
    'HMAC-SHA256 matches RFC 4231 case 2';

# ---- ZMTP 3.0, the client half ----------------------------------------------

sub greeting(--> Buf) {
    my @g = 0xFF, |(0 xx 8), 0x7F, 3, 0;
    @g.push(|'NULL'.encode('utf8').list);
    @g.push(0) while @g.elems < 32;
    @g.push(0) while @g.elems < 64;
    Buf.new(@g);
}

sub zframe($body, Bool $more, Bool $command = False --> Buf) {
    my $flags = ($more ?? 1 !! 0) +| ($command ?? 4 !! 0);
    my @out;
    if $body.elems < 256 {
        @out = $flags, $body.elems;
    }
    else {
        @out = ($flags +| 2);
        for (0..7).reverse -> $i { @out.push(($body.elems +> (8 * $i)) +& 0xFF) }
    }
    Buf.new(@out, |$body.list);
}

sub zcommand(Str $name, $body --> Buf) {
    zframe(Buf.new($name.chars, |$name.encode('utf8').list, |$body.list), False, True);
}

sub ready-props(Str $type --> Buf) {
    my @p = 'Socket-Type'.chars, |'Socket-Type'.encode('utf8').list;
    @p.push(0, 0, 0, $type.chars);
    @p.push(|$type.encode('utf8').list);
    Buf.new(@p);
}

sub zconnect(Int $port, Str $type) {
    my $s = IO::Socket::INET.new(:host<127.0.0.1>, :port($port));
    $s.write(greeting());
    my $peer = $s.read(64);
    die "the kernel sent a {$peer.elems}-byte greeting" unless $peer.elems == 64;
    die "the kernel's greeting has no ZMTP signature" unless $peer[0] == 0xFF && $peer[9] == 0x7F;
    $s.write(zcommand('READY', ready-props($type)));
    my %c = sock => $s;
    if $type eq 'SUB' {
        # ZMTP 3.0 spells a subscription as a message frame: 0x01 then the
        # prefix, empty here — this client wants every topic.
        $s.write(zframe(Buf.new(1), False));
    }
    %c;
}

sub zsend(%c, @frames) {
    my @bytes;
    for @frames.kv -> $i, $f {
        @bytes.push(|zframe($f, $i + 1 < @frames.elems).list);
    }
    %c<sock>.write(Buf.new(@bytes));
}

# One message, commands (READY, PING) skipped — they are the transport talking
# to itself, not something a kernel said.
sub zrecv(%c) {
    my @frames;
    loop {
        my $f = %c<sock>.read(1);
        die 'the kernel closed the connection' unless $f && $f.elems;
        my $flags = $f[0];
        my $size;
        if $flags +& 2 {
            my $b = %c<sock>.read(8);
            $size = 0;
            for ^8 -> $i { $size = ($size +< 8) +| $b[$i] }
        }
        else {
            $size = %c<sock>.read(1)[0];
        }
        my $body = $size ?? %c<sock>.read($size) !! Buf.new;
        next if $flags +& 4;
        @frames.push($body);
        last unless $flags +& 1;
    }
    @frames;
}

# ---- the Jupyter message layer, the client half -----------------------------

my $KEY     = 'smoke-key-0123456789abcdef';
my $SESSION = 'jupyter-smoke-session';
my $msgn    = 0;

sub jframes(Str $type, Str $content, Bool :$forge = False) {
    my $id = 'smoke-' ~ $msgn++;
    my $header = '{"msg_id":"' ~ $id ~ '","session":"' ~ $SESSION ~
                 '","username":"smoke","date":"2026-01-01T00:00:00.000000Z","msg_type":"' ~
                 $type ~ '","version":"5.3"}';
    my $parent = '{}';
    my $meta   = '{}';
    my $sig = hex(hmac-sha256($KEY.encode('utf8'),
                              ($header ~ $parent ~ $meta ~ $content).encode('utf8')));
    $sig = 'de' x 32 if $forge;
    # A hash, not a two-element list: a list whose tail is an Array is one
    # element, and `my ($id, @frames) = ...` would bind the whole thing to
    # @frames[0].
    %( id => $id,
       frames => ['<IDS|MSG>', $sig, $header, $parent, $meta, $content].map(*.encode('utf8')).Array );
}

# A field's string value, read out of the raw JSON. The gate deliberately does
# not parse JSON: what it pins is the bytes the kernel wrote.
sub field(Str $json, Str $key) {
    my $needle = '"' ~ $key ~ '":"';
    return '' unless $json.contains($needle);
    $json.split($needle)[1].split('"')[0];
}

sub jdecode(@frames) {
    my @s = @frames.map(*.decode('utf8'));
    my $d = 0;
    for @s.kv -> $i, $v { if $v eq '<IDS|MSG>' { $d = $i; last } }
    my %m =
        sig     => @s[$d + 1],
        header  => @s[$d + 2],
        parent  => @s[$d + 3],
        meta    => @s[$d + 4],
        content => @s[$d + 5];
    %m<type>   = field(%m<header>, 'msg_type');
    %m<parent-id> = field(%m<parent>, 'msg_id');
    %m<signed> = %m<sig> eq hex(hmac-sha256($KEY.encode('utf8'),
                    (%m<header> ~ %m<parent> ~ %m<meta> ~ %m<content>).encode('utf8')));
    %m;
}

sub execute-content(Str $code) {
    my $esc = $code.subst('\\', '\\\\', :g).subst('"', '\\"', :g);
    '{"code":"' ~ $esc ~ '","silent":false,"store_history":true,' ~
    '"user_expressions":{},"allow_stdin":false,"stop_on_error":true}';
}

# ---- launch the kernel ------------------------------------------------------

my $base = 41000 + ($*PID % 4000);
my %ports = shell => $base, iopub => $base + 1, stdin => $base + 2,
            control => $base + 3, hb => $base + 4;

my $connfile = $*TMPDIR.add("rakupp-jupyter-smoke-{$*PID}.json");
$connfile.spurt: qq:to/JSON/;
    \{
      "transport": "tcp",
      "ip": "127.0.0.1",
      "signature_scheme": "hmac-sha256",
      "key": "$KEY",
      "shell_port": %ports<shell>,
      "iopub_port": %ports<iopub>,
      "stdin_port": %ports<stdin>,
      "control_port": %ports<control>,
      "hb_port": %ports<hb>,
      "kernel_name": "raku"
    \}
    JSON

my $proc = Proc::Async.new($rakupp.Str, '--jupyter', $connfile.Str);
my $kernel-err = '';
$proc.stderr.tap({ $kernel-err ~= $_ });
my $running = $proc.start;

# The kernel exits the moment it cannot bind, so a connection that keeps being
# refused means the ports were taken, not that the kernel is slow.
my %shell;
my $up = False;
for ^60 {
    %shell = try zconnect(%ports<shell>, 'DEALER');
    if %shell && %shell<sock> { $up = True; last }
    sleep 0.1;
}
unless $up {
    say "NOT OK - the kernel did not come up on port %ports<shell>";
    say $kernel-err.indent(4) if $kernel-err;
    say "jupyter-smoke: FAILED";
    exit 1;
}
check True, 'the kernel binds its ports and completes a ZMTP handshake';

my %iopub   = zconnect(%ports<iopub>, 'SUB');
my %control = zconnect(%ports<control>, 'DEALER');
my %hb      = zconnect(%ports<hb>, 'REQ');

# A wedged read would otherwise hang a CI job until its own timeout; this gate
# would rather fail loudly.
start {
    sleep 120;
    note 'jupyter-smoke: TIMED OUT waiting for the kernel';
    note $kernel-err if $kernel-err;
    $proc.kill;          # never leave a kernel holding the ports
    exit 1;
}

sub drain-iopub(Str $parent-id) {
    my @msgs;
    loop {
        my %m = jdecode(zrecv(%iopub));
        @msgs.push(%m);
        last if %m<type> eq 'status'
             && %m<content>.contains('"execution_state":"idle"')
             && %m<parent-id> eq $parent-id;
    }
    @msgs;
}

sub run-cell(Str $code) {
    my %m = jframes('execute_request', execute-content($code));
    zsend(%shell, %m<frames>);
    my %reply = jdecode(zrecv(%shell));
    my @io = drain-iopub(%m<id>);
    %( reply => %reply, io => @io );
}

# ---- kernel_info ------------------------------------------------------------

my %ki-req = jframes('kernel_info_request', '{}');
zsend(%shell, %ki-req<frames>);
my %ki = jdecode(zrecv(%shell));
check %ki<type> eq 'kernel_info_reply', 'kernel_info_request is answered', %ki<type>;
check %ki<signed>, 'the kernel signs its replies with HMAC-SHA256 the client can verify';
check %ki<parent-id> eq %ki-req<id>, 'the reply carries the request as its parent';
check %ki<content>.contains('"implementation":"rakupp"'), 'the kernel names itself rakupp', %ki<content>;
check %ki<content>.contains('"name":"raku"') && %ki<content>.contains('"file_extension":".raku"'),
    'language_info describes Raku', %ki<content>;
check %ki<content>.contains('"protocol_version":"5.3"'), 'it speaks protocol 5.3', %ki<content>;

# ---- a cell that prints AND has a value -------------------------------------

my %c1 = run-cell('say "hi"; 6 * 7');
check %c1<reply><content>.contains('"status":"ok"'), 'execute_reply says ok', %c1<reply><content>;
check %c1<reply><content>.contains('"execution_count":1'), 'the first cell is execution_count 1',
    %c1<reply><content>;

my @io1 = %c1<io>.list;
sub of(@msgs, Str $type) { @msgs.grep({ $_<type> eq $type }) }

check of(@io1, 'status').grep({ $_<content>.contains('"execution_state":"busy"') }),
    'iopub shows the kernel go busy';
check of(@io1, 'execute_input').grep({ $_<content>.contains('6 * 7') }),
    'iopub echoes the code as execute_input';
check of(@io1, 'stream').grep({ $_<content>.contains('"name":"stdout"') && $_<content>.contains('hi') }),
    'what the cell printed arrives as a stdout stream',
    of(@io1, 'stream').map({ $_<content> }).join("\n");
check of(@io1, 'execute_result').grep({ $_<content>.contains('"text/plain":"42"') }),
    "the cell's value comes back as execute_result",
    of(@io1, 'execute_result').map({ $_<content> }).join("\n");
check @io1[*-1]<content>.contains('"execution_state":"idle"'),
    'the busy lamp goes out last', @io1[*-1]<content>;
check @io1.grep({ !$_<signed> }).elems == 0, 'every iopub message is signed too';

# ---- the session: what a kernel is for --------------------------------------

run-cell('my $x = 41');
my %c3 = run-cell('$x + 1');
check %c3<io>.grep({ $_<type> eq 'execute_result' && $_<content>.contains('"text/plain":"42"') }),
    'a variable from an earlier cell is still there',
    %c3<io>.map({ $_<type> }).join(', ');

# The engine's own claim, in a notebook: rationals are exact.
my %c4 = run-cell('0.1 + 0.2 == 0.3');
check %c4<io>.grep({ $_<type> eq 'execute_result' && $_<content>.contains('"text/plain":"True"') }),
    'decimal arithmetic is exact (0.1 + 0.2 == 0.3)',
    %c4<io>.grep({ $_<type> eq 'execute_result' }).map({ $_<content> }).join("\n");

# ---- a cell that dies -------------------------------------------------------

my %c5 = run-cell('die "boom"');
check %c5<reply><content>.contains('"status":"error"'), 'a dying cell replies with error status',
    %c5<reply><content>;
check %c5<io>.grep({ $_<type> eq 'error' && $_<content>.contains('boom') }),
    'the error crosses on iopub with its message',
    %c5<io>.grep({ $_<type> eq 'error' }).map({ $_<content> }).join("\n");

my %c6 = run-cell('$x');
check %c6<io>.grep({ $_<type> eq 'execute_result' && $_<content>.contains('"text/plain":"41"') }),
    'the session SURVIVES a dying cell';

# ---- rich output ------------------------------------------------------------

my %c7 = run-cell(Q{jupyter-display('<b>hi</b>', 'text/html')});
check %c7<io>.grep({ $_<type> eq 'display_data' && $_<content>.contains('"text/html"')
                                                && $_<content>.contains('<b>hi</b>') }),
    'jupyter-display publishes display_data with its MIME type',
    %c7<io>.grep({ $_<type> eq 'display_data' }).map({ $_<content> }).join("\n");

my %c8 = run-cell(Q{jupyter-display('# heading', :mime<text/markdown>)});
check %c8<io>.grep({ $_<type> eq 'display_data' && $_<content>.contains('"text/markdown"') }),
    'jupyter-display takes the MIME type as a named argument too',
    %c8<io>.grep({ $_<type> eq 'display_data' }).map({ $_<content> }).join("\n");

# ---- frames longer than 255 bytes ------------------------------------------
# The short/long frame split is at 256 bytes, and both directions have to cross
# it: the request carries a cell longer than that, and the answer carries a
# kilobyte of output.

my $long = 'x' x 900;
my %c9 = run-cell(Q{my $s = '} ~ $long ~ Q{'; say $s.chars; say 'y' x 1000; $s.chars});
check %c9<io>.grep({ $_<type> eq 'stream' && $_<content>.contains('900') }),
    'a cell longer than one short frame arrives whole';
check %c9<io>.grep({ $_<type> eq 'stream' && $_<content>.chars > 1000 }),
    'a kilobyte of output comes back in one long frame',
    %c9<io>.grep({ $_<type> eq 'stream' }).map({ $_<content>.chars }).join(', ');

# ---- a forged signature is not a message ------------------------------------
# The bad one goes first: if the kernel answered it, the next reply read would
# carry the FORGED message as its parent.

my %bad  = jframes('kernel_info_request', '{}', :forge);
zsend(%shell, %bad<frames>);
my %good = jframes('kernel_info_request', '{}');
zsend(%shell, %good<frames>);
my %ki2 = jdecode(zrecv(%shell));
check %ki2<parent-id> eq %good<id>,
    'a message signed with the wrong key is dropped, not answered',
    "answered {%ki2<parent-id>}, forged was {%bad<id>}";

# ---- the heartbeat ----------------------------------------------------------

zsend(%hb, ['', 'ping'].map(*.encode('utf8')).Array);
my @beat = zrecv(%hb);
check @beat.elems == 2 && @beat[1].decode('utf8') eq 'ping',
    'the heartbeat echoes what the frontend pinged it with',
    @beat.map({ $_.decode('utf8') }).join('|');

# ---- shutdown ---------------------------------------------------------------

my %sd = jframes('shutdown_request', '{"restart":false}');
zsend(%control, %sd<frames>);
my %sdr = jdecode(zrecv(%control));
check %sdr<type> eq 'shutdown_reply' && %sdr<content>.contains('"status":"ok"'),
    'shutdown_request is answered on the control channel', %sdr<content>;

my $result = await $running;
check $result.exitcode == 0, 'the kernel exits 0 after shutdown_request',
    "exit code {$result.exitcode}";

$connfile.unlink;

if $errors {
    say "jupyter-smoke: FAILED ($errors)";
    say $kernel-err.indent(4) if $kernel-err;
    $proc.kill;
    exit 1;
}
say "jupyter-smoke: ok";
