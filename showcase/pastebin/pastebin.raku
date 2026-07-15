#!/usr/bin/env raku
# A tiny pastebin — a real HTTP/1.1 server written directly on IO::Socket::INET,
# no framework. It parses requests by hand, keeps pastes in memory, and serves
# a small HTML UI plus a JSON-ish API. Compile it with `--exe` and you have a
# single native binary you can drop on a host and point a URL at.
#
#   build/rakupp showcase/pastebin/pastebin.raku            # then open http://127.0.0.1:8080
#   build/rakupp --exe -o pastebin showcase/pastebin/pastebin.raku && ./pastebin
#
#   PORT=9000 ./pastebin        # choose the port
#
# Routes:
#   GET  /                 the create form + list of recent pastes
#   POST /paste            body `content=...` (form-encoded) → 303 redirect to /p/<id>
#   GET  /p/<id>           view one paste (HTML)
#   GET  /raw/<id>         view one paste (text/plain)

constant HOST = '0.0.0.0';

# ---------- in-memory store --------------------------------------------
my %pastes;          # id => text
my @order;           # ids, newest last
my $counter = 0;

sub new-id(--> Str) {
    # short, url-safe, monotonic-ish base36 id
    $counter++;
    my $n = $counter + 1295;         # start at 3 chars
    my $s = '';
    my @d = |('0'..'9'), |('a'..'z');
    while $n > 0 { $s = @d[$n % 36] ~ $s; $n = $n div 36; }
    $s;
}

sub store-paste(Str $text --> Str) {
    my $id = new-id();
    %pastes{$id} = $text;
    @order.push($id);
    $id;
}

# ---------- HTTP helpers -----------------------------------------------
sub html-escape(Str $s --> Str) {
    $s.subst('&', '&amp;', :g).subst('<', '&lt;', :g).subst('>', '&gt;', :g);
}

# minimal percent-decoding for application/x-www-form-urlencoded
sub url-decode(Str $s is copy --> Str) {
    $s = $s.subst('+', ' ', :g);
    $s ~~ s:g/'%' (<[0..9A..Fa..f]> ** 2)/{ chr(:16(~$0)) }/;
    $s;
}

sub form-field(Str $body, Str $name --> Str) {
    for $body.split('&') -> $pair {
        my ($k, $v) = $pair.split('=', 2);
        return url-decode($v // '') if $k eq $name;
    }
    '';
}

sub response(Int $status, Str $body, Str :$type = 'text/html; charset=utf-8', :%extra --> Str) {
    my $reason = %(200 => 'OK', 303 => 'See Other', 404 => 'Not Found',
                  400 => 'Bad Request', 405 => 'Method Not Allowed'){$status} // 'OK';
    my $head = "HTTP/1.1 $status $reason\r\n"
             ~ "Content-Type: $type\r\n"
             ~ "Content-Length: {$body.encode('utf-8').bytes}\r\n"
             ~ "Connection: close\r\n";
    $head ~= "$_.key(): $_.value()\r\n" for %extra.pairs;
    $head ~ "\r\n" ~ $body;
}

# ---------- pages -------------------------------------------------------
# CSS is kept in a NON-interpolating Q heredoc so its `{ }` stay literal; the
# dynamic pages are assembled by plain concatenation with "…" interpolation
# (no braces), which sidesteps qq's code-interpolation entirely.
my $CSS = Q:to/CSS/;
    <style>
      body { font: 16px/1.5 system-ui, sans-serif; max-width: 46rem; margin: 2rem auto; padding: 0 1rem; color: #222; }
      h1 { font-size: 1.4rem; } a { color: #2563eb; }
      textarea { width: 100%; height: 14rem; font-family: ui-monospace, monospace; font-size: 0.9rem; }
      button { font-size: 1rem; padding: 0.4rem 1rem; margin-top: 0.5rem; }
      pre { background: #f4f4f5; padding: 1rem; overflow-x: auto; border-radius: 6px; }
      .muted { color: #888; font-size: 0.85rem; }
      ul { padding-left: 1.2rem; }
    </style>
    CSS

sub page(Str $title, Str $inner --> Str) {
    '<!doctype html><html><head><meta charset="utf-8"><title>'
    ~ html-escape($title) ~ '</title>' ~ $CSS ~ "</head><body>\n"
    ~ $inner
    ~ "\n<hr><p class=\"muted\">rakupp pastebin — a native HTTP server compiled from Raku.</p>\n"
    ~ '</body></html>';
}

sub home-page(--> Str) {
    my $recent = @order.reverse.head(10).map(-> $id {
        my $preview = html-escape(%pastes{$id}.lines.head // '');
        $preview = $preview.substr(0, 60) ~ '…' if $preview.chars > 60;
        '<li><a href="/p/' ~ $id ~ '">' ~ $id ~ '</a> <span class="muted">' ~ $preview ~ '</span></li>'
    }).join("\n") || '<li class="muted">none yet</li>';
    my $inner = '<h1>rakupp pastebin</h1>'
        ~ '<form method="POST" action="/paste">'
        ~ '<textarea name="content" placeholder="Paste something…" autofocus></textarea><br>'
        ~ '<button type="submit">Create paste</button></form>'
        ~ '<h2 style="font-size:1.1rem">Recent</h2><ul>' ~ "\n" ~ $recent ~ "\n</ul>";
    page('rakupp pastebin', $inner);
}

sub paste-page(Str $id --> Str) {
    return response(404, page('Not found', '<h1>404</h1><p>No such paste.</p>')) unless %pastes{$id}:exists;
    my $body = html-escape(%pastes{$id});
    my $inner = '<h1>paste <code>' ~ $id ~ '</code></h1>'
        ~ '<p class="muted"><a href="/raw/' ~ $id ~ '">raw</a> · <a href="/">new paste</a></p>'
        ~ '<pre>' ~ $body ~ '</pre>';
    response(200, page("paste $id", $inner));
}

# ---------- request routing --------------------------------------------
sub handle-request(Str $method, Str $path, Str $body --> Str) {
    if $method eq 'GET' && $path eq '/' {
        return response(200, home-page());
    }
    elsif $method eq 'POST' && $path eq '/paste' {
        my $content = form-field($body, 'content');
        return response(400, page('Empty', '<h1>Empty paste</h1><p><a href="/">back</a></p>')) unless $content.trim;
        my $id = store-paste($content);
        return response(303, '', :extra(%('Location' => "/p/$id")));
    }
    elsif $method eq 'GET' && $path ~~ /^ '/p/' (<[0..9a..z]>+) $/ {
        return paste-page(~$0);
    }
    elsif $method eq 'GET' && $path ~~ /^ '/raw/' (<[0..9a..z]>+) $/ {
        my $id = ~$0;
        return response(404, 'not found', :type('text/plain')) unless %pastes{$id}:exists;
        return response(200, %pastes{$id}, :type('text/plain; charset=utf-8'));
    }
    response(404, page('Not found', '<h1>404</h1><p><a href="/">home</a></p>'));
}

# Read a full HTTP request from the socket: headers, then Content-Length bytes.
sub read-request($conn --> Str) {
    my $data = $conn.recv // '';
    # if there's a body we haven't fully read yet, keep pulling
    if $data ~~ /'Content-Length:' \s* (\d+)/ {
        my $need = (~$0).Int;
        my $sep = $data.index("\r\n\r\n");
        if $sep.defined {
            my $have = $data.substr($sep + 4).encode('utf-8').bytes;
            while $have < $need {
                my $more = $conn.recv // '';
                last if $more eq '';
                $data ~= $more;
                $have += $more.encode('utf-8').bytes;
            }
        }
    }
    $data;
}

sub parse-request(Str $raw) {
    my $sep = $raw.index("\r\n\r\n") // $raw.chars;
    my $head = $raw.substr(0, $sep);
    my $body = $raw.substr($sep + 4);
    my $reqline = $head.lines.head // '';
    my ($method, $path) = $reqline.split(' ');
    ($method // 'GET', $path // '/', $body // '');
}

sub MAIN($port-arg?) {
    my $port = ($port-arg // %*ENV<PORT> // 8080).Int;
    my $listener = IO::Socket::INET.new(:localhost(HOST), :localport($port), :listen);
    note "rakupp pastebin listening on http://127.0.0.1:$port  (Ctrl-C to stop)";
    loop {
        my $conn = $listener.accept;
        my $raw = read-request($conn);
        my ($method, $path, $body) = parse-request($raw);
        note "  $method $path";
        my $reply = handle-request($method, $path, $body);
        $conn.print($reply);
        $conn.close;
    }
}
