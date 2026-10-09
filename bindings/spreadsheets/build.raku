#!/usr/bin/env rakupp
# build.raku — assembles the Excel add-in and the Google Sheets script.
#
#   rakupp bindings/spreadsheets/build.raku --base=https://example.org/raku-excel/
#
# writes dist/excel (the files to serve, the engine among them, and the
# manifest that points at them), dist/google-sheets (the files of an Apps
# Script project attached to a spreadsheet, whose sidebar loads the engine
# from the same address) and dist/google-sheets-addon (that project as a
# Marketplace add-on). The engine is a Raku.js build (rakujs/playground by
# default).

use Data::Native :digest;

my $HERE = $*PROGRAM.IO.absolute.IO.parent;
my $ROOT = $HERE.parent.parent;

# A Str as a JSON string literal, escaped the way JSON::Fast's to-json does it.
# The build needs nothing installed, so the release job (which has no
# modules) runs it as is.
sub json-string(Str $s --> Str) {
    my %esc = "\\" => "\\\\", '"' => '\\"', "\n" => '\\n', "\r" => '\\r', "\t" => '\\t';
    '"' ~ $s.subst(/<[\\"\x00..\x1f\x10000..\x10FFFF]>/, {
        my $c = .ord;
        %esc{~$_} // ($c < 0x10000 ?? sprintf('\\u%04x', $c)       # other controls
                     !! sprintf('\\u%04X\\u%04X',                   # beyond the BMP: a surrogate pair
                                0xD800 + (($c - 0x10000) +> 10), 0xDC00 + (($c - 0x10000) +& 0x3FF)))
    }, :g) ~ '"'
}

sub MAIN(
    Str :$base   = 'https://localhost:3000/',   #= HTTPS address the Excel files will be served from, ending in /
    Str :$rakujs = ~$ROOT.add('rakujs/playground'),   #= directory holding rakujs.js and rakujs.wasm
    Str :$out    = ~$HERE.add('dist'),           #= where to write excel/, google-sheets/ and google-sheets-addon/
    Int :$revision = 0,                          #= the fourth number of the Excel manifest's version, raised to resubmit it between releases
    Bool :$store,                                #= also write store/: the icons the Marketplace and AppSource listings take
) {
    die "--base must end with a slash: $base\n" unless $base.ends-with('/');
    note "warning: Excel loads add-ins over HTTPS only; $base will not work there" unless $base.starts-with('https://');
    my $engine-js   = $rakujs.IO.add('rakujs.js');
    my $engine-wasm = $rakujs.IO.add('rakujs.wasm');
    die "no Raku.js build in $rakujs (run rakujs/build.sh)\n" unless $engine-js.e && $engine-wasm.e;

    my $version = repo-version($revision);
    my $core    = $HERE.add('core/rakusheet-core.js').slurp
                    .subst("'@RAKUSHEET_DRIVER@'", json-string($HERE.add('rakusheet.raku').slurp));
    die "core/rakusheet-core.js lost its @RAKUSHEET_DRIVER@ slot\n" if $core.contains('@RAKUSHEET_DRIVER@');
    # The stamp Office's cache keys on (?v=…): a digest of everything the add-in
    # is made from, so a new engine or a changed file gets a new one, and the
    # same inputs build the same files, byte for byte — raku.online commits
    # what this writes, and a rebuild with nothing new must not show up as a diff.
    my $tag = sha256-hex(join "\0", $base, $version, $core, sha256-hex($engine-js), sha256-hex($engine-wasm),
                         |<manifest.xml functions.json functions.js taskpane.html taskpane.css taskpane.js
                            rakusheet-worker.js>.map({ $HERE.add("excel/$_").slurp })).substr(0, 10);

    build-excel($out.IO.add('excel'), :$base, :$version, :$tag, :$core, :$engine-js, :$engine-wasm);
    build-sheets($out.IO, :$base);
    build-store($out.IO.add('store')) if $store;
}

# CMakeLists.txt's VERSION, as the four numbers an Office manifest wants.
# AppSource takes a changed manifest only with a higher version, so a
# resubmission between releases raises the fourth.
sub repo-version(Int $revision --> Str) {
    my $cmake = $ROOT.add('CMakeLists.txt').slurp;
    my $v = ($cmake ~~ / VERSION \s+ (\d+ '.' \d+ '.' \d+) /) ?? ~$0 !! '0.0.0';
    "$v.$revision"
}

sub fresh(IO::Path $dir) {
    if $dir.d {
        for $dir.dir -> $f { $f.d ?? fresh($f) !! $f.unlink }
    }
    $dir.mkdir;
    $dir
}

sub fill(Str $text, %slots --> Str) {
    my $t = $text;
    for %slots.kv -> $k, $v { $t = $t.subst('@' ~ $k ~ '@', $v, :g) }
    die 'an unfilled slot is left: @' ~ $0 ~ "@\n" if $t ~~ / '@' (<[A..Z_]>+) '@' /;
    $t
}

sub build-excel(IO::Path $dir, :$base, :$version, :$tag, :$core, :$engine-js, :$engine-wasm) {
    fresh($dir);
    my %slots = BASE => $base, VERSION => $version, TAG => $tag;
    for <manifest.xml functions.json functions.js taskpane.html taskpane.css taskpane.js rakusheet-worker.js> -> $f {
        $dir.add($f).spurt: fill($HERE.add("excel/$f").slurp, %slots);
    }
    $dir.add('rakusheet-core.js').spurt: $core;
    $dir.add('rakujs.js').spurt: $engine-js.slurp;
    $dir.add('rakujs.wasm').spurt: $engine-wasm.slurp(:bin);
    my $assets = $dir.add('assets');
    $assets.mkdir;
    for 16, 32, 64, 80, 128 -> $size {
        $assets.add("icon-$size.png").spurt: icon($size);
    }
    say "excel:         $dir  (manifest points at $base)";
}

# The listings' pictures that this can draw: the Marketplace's application
# icons (32 and 128) and the OAuth consent screen's logo (120), and
# AppSource's logo (216 to 350; 300). store/banner.html is the Marketplace's
# card banner, which needs a browser to draw its words (store/README.md).
sub build-store(IO::Path $dir) {
    fresh($dir);
    $dir.add("icon-$_.png").spurt: icon($_) for 32, 120, 128, 300;
    $dir.add('banner.html').spurt: $HERE.add('store/banner.html').slurp;
    say "store:         $dir  (icons for the listings, and banner.html)";
}

# The Apps Script project twice: google-sheets/, the script attached to one
# spreadsheet, and google-sheets-addon/, the same as a Marketplace add-on.
# They differ in Raku.gs's marked region (google-sheets/RakuAddon.js has the
# add-on's) and in appsscript.json. Neither holds the engine: the Raku
# sidebar loads it from --base, where the Excel add-in's files are, and keeps
# it in the browser; its pane looks like Excel's, whose taskpane.css it takes.
sub build-sheets(IO::Path $out, :$base) {
    my $raku = $HERE.add('google-sheets/Raku.js').slurp;
    my @own = $raku.match(/ ^^ '// ---- the attached script' \N* \n .*? ^^ '// ---- end of the attached script' \N* \n /, :g);
    die "google-sheets/Raku.js should have one attached-script region, not {+@own}\n" unless @own == 1;
    my %script =
        'appsscript.json'  => $HERE.add('google-sheets/appsscript.json').slurp,
        'Raku.gs'          => $raku,
        'RakuSidebar.html' => fill($HERE.add('google-sheets/RakuSidebar.html').slurp,
                                   %(BASE => $base, CSS => $HERE.add('excel/taskpane.css').slurp.chomp));
    my %addon = %script;
    %addon<appsscript.json> = $HERE.add('google-sheets/appsscript-addon.json').slurp;
    %addon<Raku.gs> = $raku.substr(0, @own[0].from) ~ $HERE.add('google-sheets/RakuAddon.js').slurp ~ $raku.substr(@own[0].to);

    for 'google-sheets', %script, 'google-sheets-addon', %addon -> $name, %files {
        my $dir = fresh($out.add($name));
        $dir.add(.key).spurt(.value) for %files.sort(*.key);
    }
    say "google-sheets: {$out.add('google-sheets')}  (the engine in a sidebar, from $base)";
    say "               {$out.add('google-sheets-addon')}  (the same, as a Marketplace add-on)";
}

# ---- the icon: a white R++ on an indigo rounded square, drawn here so that
# ---- the repository holds no binary files.

my constant BG = (0x43, 0x38, 0xCA);

sub in-square(Num $x, Num $y --> Bool) {
    my $r = 0.2e0;
    my $dx = max(abs($x - 0.5e0) - (0.5e0 - $r), 0e0);
    my $dy = max(abs($y - 0.5e0) - (0.5e0 - $r), 0e0);
    $dx * $dx + $dy * $dy <= $r * $r
}

# The R, drawn in a unit box and placed by in-glyph.
sub in-letter(Num $x, Num $y --> Bool) {
    return True if 0.28e0 <= $x <= 0.42e0 && 0.18e0 <= $y <= 0.82e0;                 # stem
    return True if 0.28e0 <= $x <= 0.50e0 && (0.18e0 <= $y <= 0.30e0 || 0.50e0 <= $y <= 0.62e0);   # bars
    if $x >= 0.50e0 {                                                                 # bowl
        my $d = sqrt(($x - 0.50e0) ** 2 + ($y - 0.40e0) ** 2);
        return True if 0.10e0 <= $d <= 0.22e0;
    }
    # the leg: within 0.065 of the segment (0.50, 0.56)–(0.74, 0.82)
    my ($ax, $ay, $bx, $by) = 0.50e0, 0.56e0, 0.74e0, 0.82e0;
    my $t = (($x - $ax) * ($bx - $ax) + ($y - $ay) * ($by - $ay)) / (($bx - $ax) ** 2 + ($by - $ay) ** 2);
    $t = min(max($t, 0e0), 1e0);
    my $d = sqrt(($x - $ax - $t * ($bx - $ax)) ** 2 + ($y - $ay - $t * ($by - $ay)) ** 2);
    $d <= 0.065e0 && $y <= 0.82e0
}

# The R, scaled to 0.70 and moved left, then two pluses on its middle line.
sub in-glyph(Num $x, Num $y --> Bool) {
    my $scale = 0.70e0;
    return True if in-letter(($x + 0.096e0) / $scale, ($y - 0.15e0) / $scale);
    in-plus($x, $y, 0.615e0) || in-plus($x, $y, 0.835e0)
}

sub in-plus(Num $x, Num $y, Num $cx --> Bool) {
    my ($arm, $half) = 0.095e0, 0.040e0;
    my ($dx, $dy) = abs($x - $cx), abs($y - 0.50e0);
    ($dx <= $arm && $dy <= $half) || ($dx <= $half && $dy <= $arm)
}

sub icon(Int $s --> Blob) {
    my $pixels = Buf.new;
    for ^$s -> $py {
        for ^$s -> $px {
            my ($back, $fore) = 0, 0;
            for ^4 -> $sy {
                for ^4 -> $sx {
                    my $x = ($px + ($sx + 0.5e0) / 4e0) / $s;
                    my $y = ($py + ($sy + 0.5e0) / 4e0) / $s;
                    next unless in-square($x, $y);
                    in-glyph($x, $y) ?? $fore++ !! $back++;
                }
            }
            my $covered = $back + $fore;
            if $covered == 0 { $pixels.append(0, 0, 0, 0); next }
            my $white = $fore / $covered;
            $pixels.append(|BG.map({ ($_ * (1 - $white) + 255 * $white).round }), ($covered / 16 * 255).round);
        }
    }
    png($s, $s, $pixels)
}

sub be32(Int $n --> Blob) { Blob.new(($n +> 24) +& 255, ($n +> 16) +& 255, ($n +> 8) +& 255, $n +& 255) }

sub crc32(Blob $b --> Int) {
    state @table = (^256).map: -> $n {
        my $c = $n;
        for ^8 { $c = $c +& 1 ?? 0xEDB88320 +^ ($c +> 1) !! $c +> 1 }
        $c
    };
    my $c = 0xFFFFFFFF;
    for $b.list -> $byte { $c = @table[($c +^ $byte) +& 255] +^ ($c +> 8) }
    $c +^ 0xFFFFFFFF
}

sub adler32(Blob $b --> Int) {
    my ($a, $s) = 1, 0;
    for $b.list -> $byte {
        $a = ($a + $byte) % 65521;
        $s = ($s + $a) % 65521;
    }
    $s +< 16 +| $a
}

sub png-chunk(Str $type, Blob $data --> Blob) {
    my $t = $type.encode('ascii');
    be32($data.elems) ~ $t ~ $data ~ be32(crc32($t ~ $data))
}

# RGBA, 8 bits a channel, no filtering, and DEFLATE's stored blocks: an icon
# is a few kilobytes either way.
sub png(Int $w, Int $h, Blob $rgba --> Blob) {
    my $raw = Buf.new;
    for ^$h -> $y {
        $raw.append(0);
        $raw.append($rgba.subbuf($y * $w * 4, $w * 4));
    }
    my $z = Buf.new(0x78, 0x01);
    my $pos = 0;
    while $pos < $raw.elems {
        my $len = min(65535, $raw.elems - $pos);
        my $final = $pos + $len >= $raw.elems ?? 1 !! 0;
        $z.append($final, $len +& 255, $len +> 8, +^$len +& 255, (+^$len +> 8) +& 255);
        $z.append($raw.subbuf($pos, $len));
        $pos += $len;
    }
    $z.append(be32(adler32($raw)));
    Blob.new(137, 80, 78, 71, 13, 10, 26, 10)
        ~ png-chunk('IHDR', be32($w) ~ be32($h) ~ Blob.new(8, 6, 0, 0, 0))
        ~ png-chunk('IDAT', $z)
        ~ png-chunk('IEND', Blob.new)
}
