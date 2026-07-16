#!/usr/bin/env raku
# rakus — a static HTTP file server, on nothing but IO::Socket::INET.
#
# Point it at a directory and it serves the files inside over HTTP/1.1: correct
# Content-Type by extension (text *and* binary), auto directory listings when
# there's no index.html, HEAD as well as GET, and 301/403/404/405 where they
# belong. Every connection is handled on its own thread. It is the general
# "web server" to the pastebin's single-purpose app — the same raw sockets, but
# a reusable server rather than one hand-wired route table.
#
#   build/rakupp showcase/rakus/rakus.raku                  # serves ./public on :8080
#   build/rakupp showcase/rakus/rakus.raku 9000             # choose the port
#   build/rakupp showcase/rakus/rakus.raku 9000 /var/www    # choose port and root
#   build/rakupp --exe -o rakus showcase/rakus/rakus.raku && ./rakus 8080 ~/site
#
# Then open http://127.0.0.1:8080/ in a browser.

constant HOST = '0.0.0.0';
my $ROOT;                                  # absolute path of the served directory

# ---------- MIME types --------------------------------------------------
my %MIME =
    html => 'text/html; charset=utf-8',   htm  => 'text/html; charset=utf-8',
    css  => 'text/css; charset=utf-8',    js   => 'application/javascript; charset=utf-8',
    mjs  => 'application/javascript; charset=utf-8',
    json => 'application/json; charset=utf-8', xml => 'application/xml; charset=utf-8',
    txt  => 'text/plain; charset=utf-8',  md   => 'text/markdown; charset=utf-8',
    csv  => 'text/csv; charset=utf-8',
    svg  => 'image/svg+xml',              png  => 'image/png',
    jpg  => 'image/jpeg',                 jpeg => 'image/jpeg',
    gif  => 'image/gif',                  webp => 'image/webp',
    ico  => 'image/x-icon',               pdf  => 'application/pdf',
    wasm => 'application/wasm',            zip  => 'application/zip',
    woff2 => 'font/woff2';

sub mime-for(Str $path --> Str) {
    my $ext = ($path ~~ / '.' (\w+) $/) ?? (~$0).lc !! '';
    %MIME{$ext} // 'application/octet-stream';
}

# ---------- small helpers ----------------------------------------------
sub esc(Str $s --> Str) {
    $s.subst('&', '&amp;', :g).subst('<', '&lt;', :g).subst('>', '&gt;', :g).subst('"', '&quot;', :g);
}
sub url-decode(Str $s is copy --> Str) {
    $s ~~ s:g/ '%' (<[0..9A..Fa..f]> ** 2) /{ chr(:16(~$0)) }/;
    $s;
}
sub human-size(Int $n --> Str) {
    return "$n B" if $n < 1024;
    return sprintf("%.1f KB", $n / 1024) if $n < 1024 * 1024;
    sprintf("%.1f MB", $n / (1024 * 1024));
}

my %REASON = 200 => 'OK', 301 => 'Moved Permanently', 400 => 'Bad Request',
             403 => 'Forbidden', 404 => 'Not Found', 405 => 'Method Not Allowed';

# ---------- page rendering ---------------------------------------------
# CSS lives in a non-interpolating Q heredoc so its `{ }` stay literal; the pages
# are assembled by concatenation, which sidesteps qq's code interpolation.
my $CSS = Q:to/CSS/;
    <style>
      body { font: 15px/1.5 system-ui, sans-serif; max-width: 48rem; margin: 2.5rem auto; padding: 0 1rem; color: #222; }
      h1 { font-size: 1.2rem; } a { color: #2563eb; text-decoration: none; } a:hover { text-decoration: underline; }
      ul { list-style: none; padding: 0; } li { padding: 0.15rem 0; }
      .sz { color: #999; font-size: 0.85em; margin-left: 0.5rem; }
      .muted { color: #888; font-size: 0.85rem; margin-top: 2rem; }
      code { background: #f2f2f4; padding: 0.1em 0.3em; border-radius: 4px; }
    </style>
    CSS

sub page(Str $title, Str $inner --> Str) {
    '<!doctype html><html><head><meta charset="utf-8">'
    ~ '<meta name="viewport" content="width=device-width, initial-scale=1">'
    ~ '<title>' ~ esc($title) ~ '</title>' ~ $CSS ~ "</head><body>\n"
    ~ $inner
    ~ "\n<p class=\"muted\">served by rakus — a Raku++ static file server</p>\n"
    ~ '</body></html>';
}

sub dir-listing(IO::Path $dir, Str $urlpath --> Str) {
    my @entries = dir($dir).map(*.IO).sort({ (!.d, .basename.lc) });  # dir() yields Str paths
    my $rows = '';
    $rows ~= '<li><a href="../">📁 ../</a></li>' ~ "\n" unless $urlpath eq '/';
    for @entries -> $e {
        my $name = $e.basename;
        next if $name.starts-with('.');                       # hide dotfiles
        my $disp = $name ~ ($e.d ?? '/' !! '');
        my $icon = $e.d ?? '📁' !! '📄';
        my $size = $e.d ?? '' !! ' <span class="sz">' ~ human-size($e.s) ~ '</span>';
        $rows ~= '<li><a href="' ~ esc($disp) ~ '">' ~ $icon ~ ' ' ~ esc($disp) ~ '</a>' ~ $size ~ '</li>' ~ "\n";
    }
    page('Index of ' ~ $urlpath,
         '<h1>Index of ' ~ esc($urlpath) ~ '</h1><ul>' ~ "\n" ~ $rows ~ '</ul>');
}

sub error-page(Int $status, Str $path --> Str) {
    page($status ~ ' ' ~ %REASON{$status},
         '<h1>' ~ $status ~ ' — ' ~ %REASON{$status} ~ '</h1>'
         ~ '<p><code>' ~ esc($path) ~ '</code></p><p><a href="/">home</a></p>');
}

# ---------- request handling -------------------------------------------
# Returns (status, content-type, Buf body, extra-header-or-Nil).
sub handle(Str $method, Str $target) {
    unless $method eq 'GET' || $method eq 'HEAD' {
        return 405, 'text/html; charset=utf-8', error-page(405, $method).encode, Nil;
    }
    my $path = url-decode($target.split('?', 2)[0]);

    # path safety: no traversal, must be absolute
    return 400, 'text/html; charset=utf-8', error-page(400, $path).encode, Nil
        unless $path.starts-with('/');
    return 403, 'text/html; charset=utf-8', error-page(403, $path).encode, Nil
        if $path.split('/').any eq '..';

    my $fs = ($ROOT ~ $path).IO;

    if $fs.d {
        # a directory URL must end in '/' so relative links resolve — else redirect
        unless $path.ends-with('/') {
            return 301, 'text/html; charset=utf-8', ''.encode, ('Location' => "$path/");
        }
        my $index = "$fs/index.html".IO;
        if $index.f {
            return 200, mime-for('index.html'), $index.slurp(:bin), Nil;
        }
        return 200, 'text/html; charset=utf-8', dir-listing($fs, $path).encode, Nil;
    }
    elsif $fs.f {
        return 200, mime-for($fs.Str), $fs.slurp(:bin), Nil;
    }
    else {
        return 404, 'text/html; charset=utf-8', error-page(404, $path).encode, Nil;
    }
}

# ---------- connection loop --------------------------------------------
# Read request bytes until the header terminator; GET/HEAD carry no body.
sub read-request($conn --> Str) {
    my $data = '';
    while !$data.contains("\r\n\r\n") {
        my $chunk = $conn.recv;
        last unless $chunk.defined && $chunk ne '';
        $data ~= $chunk;
        last if $data.chars > 65536;               # header flood guard
    }
    $data;
}

sub serve($conn) {
    my $raw = read-request($conn);
    if $raw {
        my $reqline = $raw.lines.head // '';
        my ($method, $target) = $reqline.split(' ');
        $method //= 'GET'; $target //= '/';
        my ($status, $ctype, $body, $extra) = handle($method, $target);

        my $head = "HTTP/1.1 $status {%REASON{$status} // 'OK'}\r\n"
                 ~ "Content-Type: $ctype\r\n"
                 ~ "Content-Length: {$body.bytes}\r\n"
                 ~ ($extra ?? "{$extra.key}: {$extra.value}\r\n" !! '')
                 ~ "Server: rakus\r\nConnection: close\r\n\r\n";
        try {
            $conn.print($head);
            $conn.write($body) unless $method eq 'HEAD' || $body.bytes == 0;
        }
        note sprintf('  %s %-4s %s', $status, $method, $target);
    }
    try { $conn.close; }
}

sub MAIN($port-arg?, $root?) {
    my $port = ($port-arg // %*ENV<PORT> // 8080).Int;
    my $dir  = ($root // $?FILE.IO.parent.add('public').Str);
    unless $dir.IO.d {
        note "rakus: no such directory: $dir";
        exit 1;
    }
    $ROOT = $dir.IO.absolute.subst(/ '/' $ /, '');   # absolute, no trailing slash

    my $listener = IO::Socket::INET.new(:localhost(HOST), :localport($port), :listen);
    note "rakus serving $ROOT on http://127.0.0.1:$port/  (Ctrl-C to stop)";
    loop {
        my $conn = $listener.accept;
        start serve($conn);
    }
}
