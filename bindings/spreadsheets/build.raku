#!/usr/bin/env rakupp
# build.raku — assembles the Excel add-in and the Google Sheets script.
#
#   rakupp bindings/spreadsheets/build.raku --base=https://example.org/raku-excel/
#
# writes dist/excel (the files to serve, and the manifest that points at
# them), dist/google-sheets (the files of an Apps Script project attached to
# a spreadsheet) and dist/google-sheets-addon (the same project as a
# Marketplace add-on). All carry the same engine, a Raku.js build
# (rakujs/playground by default).

use JSON::Fast;
use Data::Native :digest;

my $HERE = $*PROGRAM.IO.absolute.IO.parent;
my $ROOT = $HERE.parent.parent;

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
                    .subst("'@RAKUSHEET_DRIVER@'", to-json($HERE.add('rakusheet.raku').slurp));
    die "core/rakusheet-core.js lost its @RAKUSHEET_DRIVER@ slot\n" if $core.contains('@RAKUSHEET_DRIVER@');
    # The stamp Office's cache keys on (?v=…): a digest of everything the add-in
    # is made from, so a new engine or a changed file gets a new one, and the
    # same inputs build the same files, byte for byte — raku.online commits
    # what this writes, and a rebuild with nothing new must not show up as a diff.
    my $tag = sha256-hex(join "\0", $base, $version, $core, sha256-hex($engine-js), sha256-hex($engine-wasm),
                         |<manifest.xml functions.json functions.js taskpane.html taskpane.css taskpane.js
                            rakusheet-worker.js>.map({ $HERE.add("excel/$_").slurp })).substr(0, 10);

    build-excel($out.IO.add('excel'), :$base, :$version, :$tag, :$core, :$engine-js, :$engine-wasm);
    build-sheets($out.IO, :$core, :$engine-js, :$engine-wasm);
    build-sheets-sidebar($out.IO.add('google-sheets-sidebar'), :$base);
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

# The Apps Script project twice: google-sheets/, the script attached to one
# spreadsheet, and google-sheets-addon/, the same as a Marketplace add-on.
# They differ in Raku.gs's marked region (google-sheets/RakuAddon.js has the
# add-on's) and in appsscript.json.
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

# The Sheets project that holds no engine: the Raku sidebar loads it from
# --base, where the Excel add-in's files are, and keeps it in the browser.
sub build-sheets-sidebar(IO::Path $dir, :$base) {
    fresh($dir);
    $dir.add('appsscript.json').spurt: $HERE.add('google-sheets/appsscript.json').slurp;
    $dir.add('Raku.gs').spurt: apps-script-syntax($HERE.add('google-sheets/sidebar/Raku.js').slurp);
    $dir.add('RakuSidebar.html').spurt: fill($HERE.add('google-sheets/sidebar/RakuSidebar.html').slurp, %(BASE => $base));
    say "               $dir  (the engine in a sidebar, from $base)";
}

sub build-sheets(IO::Path $out, :$core, :$engine-js, :$engine-wasm) {
    # gzip -n leaves the name and the time out, so that the same engine
    # always gives the same files.
    my $gz = run('gzip', '-9', '-n', '-c', ~$engine-wasm, :out, :bin).out.slurp(:close);
    my $b64 = base64($gz);
    my constant CHUNK = 1_000_000;   # characters per file, a multiple of 4
    my @parts = (0, CHUNK ...^ * >= $b64.chars).map({ $b64.substr($_, CHUNK) });

    my $raku = $HERE.add('google-sheets/Raku.js').slurp;
    my @own = $raku.match(/ ^^ '// ---- the attached script' \N* \n .*? ^^ '// ---- end of the attached script' \N* \n /, :g);
    die "google-sheets/Raku.js should have one attached-script region, not {+@own}\n" unless @own == 1;
    my %script =
        'appsscript.json' => $HERE.add('google-sheets/appsscript.json').slurp,
        'Raku.gs'         => apps-script-syntax($raku),
        'RakuLoader.gs'   => apps-script-syntax(fill($HERE.add('google-sheets/RakuLoader.js').slurp, %(WASM_COUNT => @parts.elems))),
        'RakuCore.gs'     => apps-script-syntax($core),
        'RakuEngine.gs'   => "// RakuEngine.gs — Raku.js, the Raku++ engine's JavaScript side, in the syntax Apps Script takes. Generated by build.raku.\n"
                             ~ apps-script-syntax($engine-js.slurp);
    for @parts.kv -> $i, $part {
        my $n = sprintf('%02d', $i + 1);
        %script{"RakuWasm$n.gs"} =
            "// RakuWasm$n.gs — part {$i + 1} of {+@parts} of the Raku engine, gzip and base64. Generated by build.raku.\n"
            ~ "var RAKUSHEET_WASM = typeof RAKUSHEET_WASM === 'undefined' ? [] : RAKUSHEET_WASM;\n"
            ~ "RAKUSHEET_WASM[$i] = \"$part\";\n";
    }
    my %addon = %script;
    %addon<appsscript.json> = $HERE.add('google-sheets/appsscript-addon.json').slurp;
    %addon<Raku.gs> = apps-script-syntax($raku.substr(0, @own[0].from) ~ $HERE.add('google-sheets/RakuAddon.js').slurp ~ $raku.substr(@own[0].to));

    for 'google-sheets', %script, 'google-sheets-addon', %addon -> $name, %files {
        my $dir = fresh($out.add($name));
        $dir.add(.key).spurt(.value) for %files.sort(*.key);
    }
    say "google-sheets: {$out.add('google-sheets')}  ({+@parts} engine files, {($gz.elems / 1048576).fmt('%.1f')} MB compressed)";
    say "               {$out.add('google-sheets-addon')}  (the same, as a Marketplace add-on)";
}

sub base64(Blob $b --> Str) {
    my @a = |('A'..'Z'), |('a'..'z'), |('0'..'9'), '+', '/';
    my @quads;
    my $n = $b.elems;
    my $i = 0;
    while $i + 3 <= $n {
        my $v = $b[$i] +< 16 +| $b[$i + 1] +< 8 +| $b[$i + 2];
        @quads.push: @a[$v +> 18] ~ @a[($v +> 12) +& 63] ~ @a[($v +> 6) +& 63] ~ @a[$v +& 63];
        $i += 3;
    }
    if $n - $i == 1 {
        my $v = $b[$i] +< 16;
        @quads.push: @a[$v +> 18] ~ @a[($v +> 12) +& 63] ~ '==';
    }
    elsif $n - $i == 2 {
        my $v = $b[$i] +< 16 +| $b[$i + 1] +< 8;
        @quads.push: @a[$v +> 18] ~ @a[($v +> 12) +& 63] ~ @a[($v +> 6) +& 63] ~ '=';
    }
    @quads.join
}

# ---- Apps Script's syntax. Apps Script reads every file with a parser of its
# ---- own when the project is saved, and that parser refuses three things the
# ---- engine's Emscripten glue has: logical assignment (a ??= b, a ||= b,
# ---- a &&= b), class fields (class C { name = "C"; … }) and BigInt literals
# ---- (0n). It takes ?. and ??, async and await, and a catch with no
# ---- binding. V8 runs all of them, so Node's test does too; these rewrite
# ---- the three into what they mean.

sub apps-script-syntax(Str $js --> Str) {
    lower-bigint-literals(lower-class-fields(lower-logical-assignment($js)))
}

# 0n is BigInt("0"), and 0xFFn is BigInt("0xFF"): the string keeps every digit.
sub lower-bigint-literals(Str $js --> Str) {
    my $s = $js;
    my @t = js-tokens($s);
    for @t.reverse.grep({ .[0] eq 'num' && $s.substr(.[2] - 1, 1) eq 'n' }) -> ($, $from, $to, $) {
        my $digits = $s.substr($from, $to - $from - 1);
        die "a BigInt literal this does not rewrite: {$digits}n\n" if $digits.contains('_') || $digits.contains('.');
        $s = $s.substr(0, $from) ~ "BigInt(\"$digits\")" ~ $s.substr($to);
    }
    $s
}

my constant PUNCT3 = set '===', '!==', '**=', '<<=', '>>=', '>>>', '&&=', '||=', '??=', '...';
my constant PUNCT2 = set '=>', '==', '!=', '<=', '>=', '&&', '||', '??', '?.', '++', '--',
                         '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '**', '<<', '>>';
# After these words a / starts a regex; after any other word it divides.
my constant REGEX-AFTER = set <return typeof instanceof in of new delete void throw case do else yield await>;
my constant LOGICAL-ASSIGN = set '??=', '||=', '&&=';

sub id-start(Int $c --> Bool) { 97 <= $c <= 122 || 65 <= $c <= 90 || $c == 95 || $c == 36 || $c > 127 }
sub id-part(Int $c --> Bool)  { id-start($c) || 48 <= $c <= 57 }
sub digit(Int $c --> Bool)    { 48 <= $c <= 57 }

# The tokens of a JavaScript text, each [kind, from, to, newline-before]:
# kind is id, num, str, tmpl (a run of a template's text), re or punct.
# A template's ${ and the } that closes it are punct tokens of their own, so
# that bracket depth counts them.
sub js-tokens(Str $s) {
    my @c = $s.comb.map(*.ord);     # by grapheme, as .substr counts
    my $n = +@c;
    my @t;
    my @subst;                      # brace depth inside each open ${ … }
    my $nl = False;
    my $i = 0;
    my sub emit(Str $kind, Int $from, Int $to) { @t.push: [$kind, $from, $to, $nl]; $nl = False }
    my sub regex-may-start(--> Bool) {
        return True unless @t;
        my ($kind, $from, $to) = @t[*-1].list;
        my $text = $s.substr($from, $to - $from);
        $kind eq 'punct' ?? $text ∉ <) ] }> !! $kind eq 'id' && $text ∈ REGEX-AFTER
    }
    # A run of a template's text, from $from to its closing backtick or its
    # next ${, read from $j on.
    my sub template-run(Int $from, Int $j is copy --> Int) {
        loop {
            die "an unterminated template literal\n" if $j >= $n;
            my $ch = @c[$j];
            if $ch == 92 { $j += 2; next }
            if $ch == 96 { emit('tmpl', $from, $j + 1); return $j + 1 }
            if $ch == 36 && $j + 1 < $n && @c[$j + 1] == 123 {
                emit('tmpl', $from, $j) if $j > $from;
                emit('punct', $j, $j + 2);
                @subst.push: 0;
                return $j + 2;
            }
            $j++;
        }
    }
    while $i < $n {
        my $ch = @c[$i];
        if $ch == 10 || $ch == 13 || $ch == 0x2028 || $ch == 0x2029 { $nl = True; $i++; next }
        if $ch == 32 || $ch == 9 || $ch == 11 || $ch == 12 || $ch == 0xA0 || $ch == 0xFEFF { $i++; next }
        if $ch == 47 && $i + 1 < $n && @c[$i + 1] == 47 {
            $i++ while $i < $n && @c[$i] != 10 && @c[$i] != 13;
            next;
        }
        if $ch == 47 && $i + 1 < $n && @c[$i + 1] == 42 {
            my $end = $s.index('*/', $i + 2) // die "an unterminated comment\n";
            $nl = True if $s.substr($i, $end - $i).contains("\n");
            $i = $end + 2;
            next;
        }
        if id-start($ch) {
            my $j = $i + 1;
            $j++ while $j < $n && id-part(@c[$j]);
            emit('id', $i, $j);
            $i = $j;
            next;
        }
        if digit($ch) || $ch == 46 && $i + 1 < $n && digit(@c[$i + 1]) {
            my $hex = $ch == 48 && $i + 1 < $n && (@c[$i + 1] == 120 || @c[$i + 1] == 88);
            my $j = $i + 1;
            loop {
                last if $j >= $n;
                my $d = @c[$j];
                if id-part($d) || $d == 46 { $j++; next }
                if !$hex && ($d == 43 || $d == 45) && (@c[$j - 1] == 101 || @c[$j - 1] == 69) { $j++; next }
                last;
            }
            emit('num', $i, $j);
            $i = $j;
            next;
        }
        if $ch == 34 || $ch == 39 {
            my $j = $i + 1;
            $j += @c[$j] == 92 ?? 2 !! 1 while $j < $n && @c[$j] != $ch;
            die "an unterminated string\n" if $j >= $n;
            emit('str', $i, $j + 1);
            $i = $j + 1;
            next;
        }
        if $ch == 96 { $i = template-run($i, $i + 1); next }
        if $ch == 125 && @subst {
            if @subst[*-1] == 0 {
                @subst.pop;
                emit('punct', $i, $i + 1);
                $i = template-run($i + 1, $i + 1);
                next;
            }
            @subst[*-1]--;
        }
        @subst[*-1]++ if $ch == 123 && @subst;
        if $ch == 47 && regex-may-start() {
            my ($j, $class) = $i + 1, False;
            loop {
                die "an unterminated regex\n" if $j >= $n || @c[$j] == 10;
                my $d = @c[$j];
                if $d == 92 { $j += 2; next }
                if $d == 91 { $class = True }
                elsif $d == 93 { $class = False }
                elsif $d == 47 && !$class { last }
                $j++;
            }
            $j++;
            $j++ while $j < $n && id-part(@c[$j]);
            emit('re', $i, $j);
            $i = $j;
            next;
        }
        my $len = 1;
        if $i + 4 <= $n && $s.substr($i, 4) eq '>>>=' { $len = 4 }
        elsif $i + 3 <= $n && $s.substr($i, 3) ∈ PUNCT3 { $len = 3 }
        elsif $i + 2 <= $n && $s.substr($i, 2) ∈ PUNCT2 {
            # ?.5 is a ? and the number .5
            $len = 2 unless $s.substr($i, 2) eq '?.' && $i + 2 < $n && digit(@c[$i + 2]);
        }
        emit('punct', $i, $i + $len);
        $i += $len;
    }
    die "an unterminated template literal\n" if @subst;
    @t
}

# The index past the last token of the expression that starts at $j: up to
# a , ; ) ] } at the starting depth, or a : that closes no ? of its own.
sub expression-end(@t, Int $j is copy, &text --> Int) {
    my ($depth, $ternary) = 0, 0;
    my $first = $j;
    while $j < @t {
        my $x = text($j);
        die "a line break inside an expression this does not rewrite\n" if $j > $first && @t[$j][3] && $depth == 0;
        if @t[$j][0] eq 'punct' {
            if $x eq any('(', '[', '{', '${') { $depth++ }
            elsif $x eq any(')', ']', '}') { last if $depth == 0; $depth-- }
            elsif $depth == 0 {
                last if $x eq ',' || $x eq ';';
                if $x eq '?' { $ternary++ }
                elsif $x eq ':' { last unless $ternary; $ternary-- }
            }
        }
        $j++;
    }
    $j
}

# The index past the bracket that closes the one at $j.
sub past-brackets(@t, Int $j is copy, &text --> Int) {
    my $depth = 0;
    loop {
        die "unbalanced brackets\n" if $j >= @t;
        my $x = text($j);
        if $x eq any('(', '[', '{', '${') { $depth++ }
        elsif $x eq any(')', ']', '}') { $depth--; return $j + 1 if $depth == 0 }
        $j++;
    }
}

# a ??= b is a ?? (a = b), and the same for ||= and &&=; the left side is a
# name or a chain of properties, which reading twice does not change.
sub lower-logical-assignment(Str $js --> Str) {
    my $s = $js;
    loop {
        my @t = js-tokens($s);
        my &text = -> $k { $s.substr(@t[$k][1], @t[$k][2] - @t[$k][1]) };
        my @ops = @t.keys.grep({ @t[$_][0] eq 'punct' && text($_) ∈ LOGICAL-ASSIGN });
        return $s unless @ops;
        my @edits;
        for @ops -> $k {
            my $end = expression-end(@t, $k + 1, &text);
            die "nothing right of a {text($k)}\n" if $end == $k + 1;
            next if @ops.first({ $k < $_ < $end });     # holds another one: the next round
            my $start = $k - 1;
            loop {
                die "a {text($k)} whose left side is not a name or a property\n" if $start < 0;
                if text($start) eq ']' {
                    die "a {text($k)} on an index this does not rewrite\n"
                        unless $start >= 3 && text($start - 2) eq '[' && @t[$start - 1][0] eq any(<id str num>);
                    $start -= 3;
                    next;
                }
                die "a {text($k)} whose left side is not a name or a property\n" unless @t[$start][0] eq 'id';
                last unless $start >= 2 && text($start - 1) eq '.';
                $start -= 2;
            }
            my $lhs = $s.substr(@t[$start][1], @t[$k][1] - @t[$start][1]).trim;
            my $rhs = $s.substr(@t[$k][2], @t[$end - 1][2] - @t[$k][2]).trim;
            @edits.push: (@t[$start][1], @t[$end - 1][2], $lhs ~ text($k).substr(0, 2) ~ "($lhs=$rhs)");
        }
        for @edits.rotor(2 => -1) -> ($a, $b) { die "logical assignments that overlap\n" if $a[1] > $b[0] }
        for @edits.reverse -> ($from, $to, $new) { $s = $s.substr(0, $from) ~ $new ~ $s.substr($to) }
    }
}

# class C { name = "C"; constructor(x) { … } } is
# class C { constructor(x) { this.name = "C"; … } }, for a class that extends
# nothing: its fields are set first thing in its constructor, in the order
# written. Assigning one rather than defining it differs only where the class
# has an accessor of the same name, which this refuses.
sub lower-class-fields(Str $js --> Str) {
    my $s = $js;
    loop {
        my @t = js-tokens($s);
        my &text = -> $k { $s.substr(@t[$k][1], @t[$k][2] - @t[$k][1]) };
        my @edits;
        for @t.keys.grep({ @t[$_][0] eq 'id' && text($_) eq 'class' && ($_ == 0 || text($_ - 1) ne any('.', '?.')) }) -> $c {
            my $j = $c + 1;
            $j++ if @t[$j][0] eq 'id' && text($j) ne 'extends';
            my $extends = text($j) eq 'extends';
            if $extends {
                $j++;
                $j = text($j) eq any('(', '[') ?? past-brackets(@t, $j, &text) !! $j + 1
                    while $j < @t && text($j) ne '{';
            }
            next unless $j < @t && text($j) eq '{';     # `class` as a property name
            my $body = $j++;
            my (@fields, %accessors, $ctor);
            until text($j) eq '}' {
                if text($j) eq ';' { $j++; next }
                my $start = $j;
                my ($static, $accessor) = False, False;
                while text($j) eq any(<static get set async>) && text($j + 1) ne any('(', '=', ';', '}') {
                    $static = True if text($j) eq 'static';
                    $accessor = True if text($j) eq any(<get set>);
                    $j++;
                }
                $j++ if text($j) eq '*';
                my $computed = text($j) eq '[';
                my $name = text($j);
                $j = $computed ?? past-brackets(@t, $j, &text) !! $j + 1;
                if text($j) eq '(' {
                    $j = past-brackets(@t, $j, &text);
                    $ctor = $j if $name eq 'constructor' && !$static;
                    %accessors{$name} = True if $accessor;
                    $j = past-brackets(@t, $j, &text);
                    next;
                }
                die "a static class field\n" if $static;
                die "a class field this does not rewrite: $name\n" if $computed || @t[$start][0] ne 'id' || $j != $start + 1;
                my $init = 'undefined';
                if text($j) eq '=' {
                    my $end = expression-end(@t, $j + 1, &text);
                    $init = $s.substr(@t[$j + 1][1], @t[$end - 1][2] - @t[$j + 1][1]);
                    $j = $end;
                }
                $j++ if text($j) eq ';';
                @fields.push: ($name, $init, @t[$start][1], @t[$j - 1][2]);
            }
            next unless @fields;
            for @fields -> ($name, $, $, $) { die "a class field beside an accessor of its name: $name\n" if %accessors{$name} }
            my $assign = @fields.map(-> ($name, $init, $, $) { "this.$name=$init;" }).join;
            die "a class field in a class that extends another\n" if $extends;
            @edits.push: $ctor.defined ?? (@t[$ctor][2], @t[$ctor][2], $assign) !! (@t[$body][2], @t[$body][2], "constructor()\{$assign\}");
            @edits.push: ($_[2], $_[3], '') for @fields;
            last;
        }
        return $s unless @edits;
        for @edits.sort(*[0]).reverse -> ($from, $to, $new) { $s = $s.substr(0, $from) ~ $new ~ $s.substr($to) }
    }
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
