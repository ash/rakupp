# MicroBit, the simulated BBC micro:bit behind `use MicroBit` (MICROBIT-PLAN.md).
# Built into rakupp as a shadow module (cmake/EmbedTools.cmake), so this copy
# is found before any -I path or store and an ecosystem `MicroBit` never
# stands in for it. Under the interpreter these objects and routines ARE the
# board; under `rakupp --target=microbit` the emitter (src/codegen/MicroBit.cpp)
# knows each name and writes CODAL's own call, so nothing here reaches a board.
#
# The names are MicroPython's, spelt as Raku: `display.scroll`,
# `button-a.is-pressed`, `accelerometer.x`, `pin0.read-analog`, `Image.HEART`.
#
# The board writes what a real one shows. The serial port is stdout. On
# stderr, each change of what the LEDs show is a line in MicroPython's own
# picture format, a brightness 0 to 9 per LED, row by row —
# `[1200 ms] display 09090:99999:99999:09990:00900` — each text shown or
# scrolled is a line, and so is each pin written. The host core a `--sim`
# build links (src/codegen/MicroBitSim.h) writes the same bytes by the same
# rules, so a program and its C++ can be compared.
#
# The clock: real time by default, so `pause(500)` waits half a second. With
# RAKUPP_MICROBIT_STOP=MS it is virtual — pause, sleep and the display's
# animations advance it instantly, every other board call advances it one
# microsecond, and the program stops when it passes MS milliseconds. Inputs,
# from the environment (docs/guide/MICROBIT.md has them all):
#   RAKUPP_MICROBIT_BUTTONS="A@1000,B@2000-2400,logo@3000"   presses and touches
#   RAKUPP_MICROBIT_ACCEL="0:0,0,-1024;1500:300,0,-950"      x,y,z over time
#   RAKUPP_MICROBIT_GESTURES="1000:shake"   _HEADING, _TEMP, _LIGHT   over time
#   RAKUPP_MICROBIT_PINS="0=1,1=512"   RAKUPP_MICROBIT_SEED=7

unit module MicroBit;

my $stop-us = do with %*ENV<RAKUPP_MICROBIT_STOP> { .Int * 1000 if .chars };
my $virtual = $stop-us.defined;
my $now-us  = 0;     # the virtual clock
my $start   = now;   # …and the real one's origin

sub clock-us() { $virtual ?? $now-us !! ((now - $start) * 1_000_000).floor }
sub stamp() { '[' ~ (clock-us() div 1000) ~ ' ms] ' }
sub advance($us) {
    return unless $virtual;
    $now-us += $us;
    if $now-us > $stop-us {
        $*ERR.print: '[' ~ ($stop-us div 1000) ~ " ms] stop\n";
        exit 0;
    }
}
sub wait-ms($ms) { if $virtual { advance $ms * 1000 } else { CORE::sleep($ms / 1000) } }

# A timeline: each entry holds from its time on; "V" alone holds from 0.
sub timeline(Str $name) {
    my $s = %*ENV{$name} // '';
    return () unless $s.chars;
    $s.split(';').map({ .contains(':') ?? (.split(':', 2)[0].Int * 1000 => .split(':', 2)[1]) !! (0 => $_) }).List
}
sub at(@line, $default) {
    my $now = clock-us();
    my $v = $default;
    for @line -> $e { $v = $e.value if $e.key <= $now }
    $v
}
sub number(@line, Int $default) { my $v = at(@line, ''); $v.chars ?? $v.Int !! $default }

my @accel    = timeline('RAKUPP_MICROBIT_ACCEL');
my @gestures = timeline('RAKUPP_MICROBIT_GESTURES');
my @heading  = timeline('RAKUPP_MICROBIT_HEADING');
my @temp     = timeline('RAKUPP_MICROBIT_TEMP');
my @light    = timeline('RAKUPP_MICROBIT_LIGHT');
# A press of button A or B, of the logo, or a touch of pin 0, 1 or 2: who,
# from, to (a hundred milliseconds unless it says).
my @presses = (%*ENV<RAKUPP_MICROBIT_BUTTONS> // '').split(',', :skip-empty).grep(*.contains('@')).map({
    my ($who, $when) = .split('@', 2);
    my ($from, $to) = $when.split('-', 2);
    ($who, $from.Int * 1000, $to.defined ?? $to.Int * 1000 !! $from.Int * 1000 + 100_000)
}).List;
my %pin-in = (%*ENV<RAKUPP_MICROBIT_PINS> // '').split(',', :skip-empty).map({ my ($p, $v) = .split('=', 2); $p.Int => $v.Int }).Hash;
my %level;
my %analog;

sub pressed(Str $who) {
    my $now = clock-us();
    so @presses.first({ .[0] eq $who && .[1] <= $now < .[2] })
}
# Presses that began after $since (-1: before the first ask) and up to now:
# CODAL counts a press as it begins.
sub presses-since(Str $who, $since) {
    my $now = clock-us();
    +@presses.grep({ .[0] eq $who && $since < .[1] <= $now })
}

my constant @GESTURES = '', 'up', 'down', 'left', 'right', 'face up', 'face down', 'freefall', '3g', '6g', '8g', 'shake';
sub known-gesture(Str() $g) {
    die "'$g' is no gesture — the accelerometer knows {@GESTURES[1..*].map({ "'$_'" }).join(', ')}" unless $g (elem) @GESTURES[1..*];
    $g
}

# randint's numbers: xorshift32, seeded with RAKUPP_MICROBIT_SEED, else 1 on
# the virtual clock (so a run repeats) and the time on the real one.
my uint32 $rng = do with %*ENV<RAKUPP_MICROBIT_SEED> { .Int if .chars } // ($virtual ?? 1 !! time);
$rng = 1 if $rng == 0;
sub randint(Int() $a, Int() $b) is export {
    die "randint's range $a..$b is empty" if $b < $a;
    my $n = $b - $a + 1;
    die "randint's range is wider than the board's random numbers" if $n > 2147483647;
    $rng +^= $rng +< 13; $rng +^= $rng +> 17; $rng +^= $rng +< 5;
    $a + $rng % $n
}

sub temperature() is export { advance 1; number(@temp, 21) }
sub running-time() is export { advance 1; clock-us() div 1000 }
sub pause(Int() $ms) is export { wait-ms($ms) if $ms > 0 }
# Raku's own sleep, in seconds, on the board's clock: to the nearest millisecond.
sub sleep(Real $seconds) is export {
    pause($seconds ~~ Int ?? $seconds * 1000 !! ($seconds.Num * 1000e0 + 0.5e0).floor)
}

# ---- the display ------------------------------------------------------------
# What the LEDs show, as CODAL keeps it: 0 to 255 per LED. MicroPython's ten
# levels map onto it by bright-map; a level is a value's highest bit (255: 9).
my constant @bright-map = 0, 1, 2, 4, 8, 16, 32, 64, 128, 255;
my @px = 0 xx 25;
my $shown = '00000:00000:00000:00000:00000';
sub level-of(Int $v) { $v >= 255 ?? 9 !! $v.msb.defined ?? $v.msb + 1 !! 0 }
sub frame-of(@p) { (^5).map(-> $y { (^5).map(-> $x { level-of(@p[$y * 5 + $x]) }).join }).join(':') }
# A picture that changes what the LEDs show is a line; one that does not is not.
sub drawn() {
    my $f = frame-of(@px);
    return if $f eq $shown;
    $shown = $f;
    $*ERR.print: stamp() ~ ($f eq '00000:00000:00000:00000:00000' ?? "display clear\n" !! "display $f\n");
}
sub bright($b) {
    die "Brightness out of range. Is: $b, should be in 0..9" unless $b ~~ Int && 0 <= $b <= 9;
    @bright-map[$b]
}
sub xy($x, $y) {
    die "Index out of range. Is: $x, should be in 0..4" unless 0 <= $x <= 4;
    die "Index out of range. Is: $y, should be in 0..4" unless 0 <= $y <= 4;
}

# CODAL's 5x5 font (codal-core's pendolino3), ASCII 32 to 126: five rows a
# glyph, two hex digits a row, the leftmost LED the row's bit 4.
my constant $FONT =
    '000000000008080800080a0a0000000a1f0a1f0a0e190e130e19120409130c120c120d0808000000'
    ~ '04080808040804040408000a040a0000040e0400000000040800000e000000000008000102040810'
    ~ '0c1212120c040c04040e1c020c101e1e0204120c060a121f021f101e011e02040e110e1f02040810'
    ~ '0e110e110e0e110e0408000800080000040004080204080402000e000e0008040204080e11060004'
    ~ '0e1115130c0c121e12121c121c121c0e1010100e1c1212121c1e101c101e1e101c10100e1013110e'
    ~ '12121e12121c0808081c1f0202120c1214181412101010101e111b15111111191513110c1212120c'
    ~ '1c121c10100c12120c061c121c12110e100c021c1f04040404121212120c1111110a041111151b11'
    ~ '12120c1212110a0404041e0408101e0e0808080e10080402010e0202020e040a000000000000001f'
    ~ '0804000000000e12120f10101c121c000e10100e02020e120e0c121c100e06081c08080e120e020c'
    ~ '10101c12120800080808020002020c10141814120808080806001b151111001c121212000c12120c'
    ~ '001c121c10000e120e02000e101010000608041808080e0807001212120f0011110a04001111151b'
    ~ '00120c0c1200110a0418001e04081e06040c0406080808080818080c081800000c0300';
sub glyph(Int $c is copy) {
    $c = 32 unless 32 <= $c <= 126;
    my $g = $FONT.substr(($c - 32) * 10, 10);
    (^5).map(-> $y { my $row = :16($g.substr($y * 2, 2)); (^5).map({ ($row +> (4 - $_)) +& 1 ?? 255 !! 0 }) }).flat.List
}
sub text($x) { $x ~~ Bool ?? ($x ?? 'True' !! 'False') !! ~$x }

class Display {
    # A picture (25 brightness levels, from `image` or Image.HEART), or text.
    method show($what) {
        if $what ~~ Positional {
            die "a picture is 25 LEDs, not {+$what}" unless $what.elems == 25;
            my @lit = $what.map(&bright);
            advance 1;
            @px = @lit;
            drawn();
            return;
        }
        my $t = text($what);
        my $n = $t.encode.bytes;
        advance 1;
        # CODAL's print: one character at once; more, each for 400 ms after a
        # first 400, then a blank, ending blank, (n + 2) × 400 ms in all.
        if $n == 1 {
            @px = glyph($t.ord);
            my $f = frame-of(@px);
            return if $f eq $shown;
            $shown = $f;
            $*ERR.print: stamp() ~ "show \"$t\"\n";
            return;
        }
        $*ERR.print: stamp() ~ "show \"$t\"\n";
        wait-ms(($n + 2) * 400);
        @px = 0 xx 25;
        $shown = frame-of(@px);
    }
    # CODAL's scroll: 120 ms a column, six columns a character (five and a
    # space), and six more to clear the last: 6 × (n + 1) × 120 ms, ending blank.
    method scroll($what) {
        die "display.scroll takes text — show a picture with display.show" if $what ~~ Positional;
        my $t = text($what);
        advance 1;
        $*ERR.print: stamp() ~ "scroll \"$t\"\n";
        wait-ms(6 * ($t.encode.bytes + 1) * 120);
        @px = 0 xx 25;
        $shown = frame-of(@px);
    }
    method clear() { advance 1; @px = 0 xx 25; drawn() }
    method set-pixel($x, $y, $b) {
        xy($x, $y);
        my $v = bright($b);
        advance 1;
        @px[$y * 5 + $x] = $v;
        drawn();
    }
    method get-pixel($x, $y) { xy($x, $y); advance 1; level-of(@px[$y * 5 + $x]) }
    method read-light-level() { advance 1; number(@light, 0) }
}
constant display is export = Display.new;

# ---- buttons, touch, pins -------------------------------------------------
class Button {
    has Str $.who;
    has $!last = -1;
    method is-pressed() { advance 1; pressed($!who) }
    method get-presses() {
        advance 1;
        my $n = presses-since($!who, $!last);
        $!last = clock-us();
        $n
    }
    method was-pressed() { self.get-presses > 0 }
}
constant button-a is export = Button.new(who => 'A');
constant button-b is export = Button.new(who => 'B');

class Logo { method is-touched() { advance 1; pressed('logo') } }
constant pin-logo is export = Logo.new;

class Pin {
    has Int $.n;
    method read-digital() {
        advance 1;
        with %pin-in{$!n} { return $_ ?? 1 !! 0 }
        %level{$!n} // 0
    }
    method write-digital($value) {
        advance 1;
        my $l = $value ?? 1 !! 0;
        return if (%level{$!n} // -1) == $l;
        %level{$!n} = $l;
        $*ERR.print: stamp() ~ "pin $!n {$l ?? 'HIGH' !! 'LOW'}\n";
    }
    method read-analog() { advance 1; %pin-in{$!n} // 0 }
    method write-analog(Int() $value) {
        my $v = ($value max 0) min 1023;
        advance 1;
        return if (%analog{$!n} // -1) == $v;
        %analog{$!n} = $v;
        $*ERR.print: stamp() ~ "pin $!n analog $v\n";
    }
    method is-touched() {
        die "pin$!n is not a touch pin — pin0, pin1 and pin2 are" unless $!n <= 2;
        advance 1;
        pressed(~$!n)
    }
}
constant pin0  is export = Pin.new(n => 0);
constant pin1  is export = Pin.new(n => 1);
constant pin2  is export = Pin.new(n => 2);
constant pin3  is export = Pin.new(n => 3);
constant pin4  is export = Pin.new(n => 4);
constant pin5  is export = Pin.new(n => 5);
constant pin6  is export = Pin.new(n => 6);
constant pin7  is export = Pin.new(n => 7);
constant pin8  is export = Pin.new(n => 8);
constant pin9  is export = Pin.new(n => 9);
constant pin10 is export = Pin.new(n => 10);
constant pin11 is export = Pin.new(n => 11);
constant pin12 is export = Pin.new(n => 12);
constant pin13 is export = Pin.new(n => 13);
constant pin14 is export = Pin.new(n => 14);
constant pin15 is export = Pin.new(n => 15);
constant pin16 is export = Pin.new(n => 16);
constant pin19 is export = Pin.new(n => 19);
constant pin20 is export = Pin.new(n => 20);

# ---- motion and the room ------------------------------------------------
class Accelerometer {
    has %!asked;   # was-gesture: when each gesture was last asked about
    # RAKUPP_MICROBIT_ACCEL: "x,y,z" in milli-g; flat, face up, unless it says otherwise
    method !axis(Int $k) {
        advance 1;
        my @a = at(@accel, '0,0,-1024').split(',').map(*.Int);
        @a[$k] // (0, 0, -1024)[$k]
    }
    method x() { self!axis(0) }
    method y() { self!axis(1) }
    method z() { self!axis(2) }
    method is-gesture(Str() $g) { known-gesture($g); advance 1; at(@gestures, '') eq $g }
    # A gesture is raised as it begins; this asks whether one has been since
    # the last ask, as the board's listener remembers it — the asking itself
    # is the program's own code, so it takes no board time.
    method was-gesture(Str() $g) {
        known-gesture($g);
        my $now = clock-us();
        my $since = %!asked{$g} // -1;
        %!asked{$g} = $now;
        so @gestures.first({ .value eq $g && $since < .key <= $now })
    }
}
constant accelerometer is export = Accelerometer.new;

class Compass {
    method heading() { advance 1; number(@heading, 0) }
    method calibrate() { advance 1; $*ERR.print: stamp() ~ "compass calibrated\n" }
}
constant compass is export = Compass.new;

# ---- pictures ---------------------------------------------------------------
# A picture is 25 brightness levels, 0 to 9, row by row from the top left.
# `image` makes one from MicroPython's text form — five rows of five digits,
# by lines or split by `:` — or from `#` (lit, 9) and `.` (dark, 0), while
# building:
#
#   constant SMILE = image q:to/END/;
#       00000
#       09090
#       ...
#       END
#
# On the board the constant is 25 bytes in flash; `image` never gets there.
sub image(Str $picture --> List) is export {
    my @rows = ($picture.contains(':') ?? $picture.split(':') !! $picture.lines).map(*.trim).grep(*.chars);
    die "a picture is 5 rows of 5, not {+@rows} rows" unless @rows == 5;
    my @levels;
    for @rows.kv -> $r, $row {
        die "row {$r + 1} of a picture has {$row.chars} LEDs, not 5" unless $row.chars == 5;
        for $row.comb -> $c {
            if    $c ~~ /<[0..9]>/     { @levels.push: +$c }
            elsif $c ~~ /<[#*Xx@]>/    { @levels.push: 9 }
            elsif $c eq '.'           { @levels.push: 0 }
            else  { die "row {$r + 1} of a picture has '$c': write a brightness 0 to 9, # for 9 or . for 0" }
        }
    }
    @levels.List
}
sub picture(Str $p) { image($p) }

# MicroPython's built-in pictures (micropython-microbit-v2's
# microbit_constimage.c, MIT licence), by its names spelt as Raku.
class Image is export {
    method HEART          { picture '09090:99999:99999:09990:00900' }
    method HEART-SMALL    { picture '00000:09090:09990:00900:00000' }
    method HAPPY          { picture '00000:09090:00000:90009:09990' }
    method SMILE          { picture '00000:00000:00000:90009:09990' }
    method SAD            { picture '00000:09090:00000:09990:90009' }
    method CONFUSED       { picture '00000:09090:00000:09090:90909' }
    method ANGRY          { picture '90009:09090:00000:99999:90909' }
    method ASLEEP         { picture '00000:99099:00000:09990:00000' }
    method SURPRISED      { picture '09090:00000:00900:09090:00900' }
    method SILLY          { picture '90009:00000:99999:00909:00999' }
    method FABULOUS       { picture '99999:99099:00000:09090:09990' }
    method MEH            { picture '09090:00000:00090:00900:09000' }
    method YES            { picture '00000:00009:00090:90900:09000' }
    method NO             { picture '90009:09090:00900:09090:90009' }
    method CLOCK12        { picture '00900:00900:00900:00000:00000' }
    method CLOCK1         { picture '00090:00090:00900:00000:00000' }
    method CLOCK2         { picture '00000:00099:00900:00000:00000' }
    method CLOCK3         { picture '00000:00000:00999:00000:00000' }
    method CLOCK4         { picture '00000:00000:00900:00099:00000' }
    method CLOCK5         { picture '00000:00000:00900:00090:00090' }
    method CLOCK6         { picture '00000:00000:00900:00900:00900' }
    method CLOCK7         { picture '00000:00000:00900:09000:09000' }
    method CLOCK8         { picture '00000:00000:00900:99000:00000' }
    method CLOCK9         { picture '00000:00000:99900:00000:00000' }
    method CLOCK10        { picture '00000:99000:00900:00000:00000' }
    method CLOCK11        { picture '09000:09000:00900:00000:00000' }
    method ARROW-N        { picture '00900:09990:90909:00900:00900' }
    method ARROW-NE       { picture '00999:00099:00909:09000:90000' }
    method ARROW-E        { picture '00900:00090:99999:00090:00900' }
    method ARROW-SE       { picture '90000:09000:00909:00099:00999' }
    method ARROW-S        { picture '00900:00900:90909:09990:00900' }
    method ARROW-SW       { picture '00009:00090:90900:99000:99900' }
    method ARROW-W        { picture '00900:09000:99999:09000:00900' }
    method ARROW-NW       { picture '99900:99000:90900:00090:00009' }
    method TRIANGLE       { picture '00000:00900:09090:99999:00000' }
    method TRIANGLE-LEFT  { picture '90000:99000:90900:90090:99999' }
    method CHESSBOARD     { picture '09090:90909:09090:90909:09090' }
    method DIAMOND        { picture '00900:09090:90009:09090:00900' }
    method DIAMOND-SMALL  { picture '00000:00900:09090:00900:00000' }
    method SQUARE         { picture '99999:90009:90009:90009:99999' }
    method SQUARE-SMALL   { picture '00000:09990:09090:09990:00000' }
    method RABBIT         { picture '90900:90900:99990:99090:99990' }
    method COW            { picture '90009:90009:99999:09990:00900' }
    method MUSIC-CROTCHET { picture '00900:00900:00900:99900:99900' }
    method MUSIC-QUAVER   { picture '00900:00990:00909:99900:99900' }
    method MUSIC-QUAVERS  { picture '09999:09009:09009:99099:99099' }
    method PITCHFORK      { picture '90909:90909:99999:00900:00900' }
    method XMAS           { picture '00900:09990:00900:09990:99999' }
    method PACMAN         { picture '09999:99090:99900:99990:09999' }
    method TARGET         { picture '00900:09990:99099:09990:00900' }
    method TSHIRT         { picture '99099:99999:09990:09990:09990' }
    method ROLLERSKATE    { picture '00099:00099:99999:99999:09090' }
    method DUCK           { picture '09900:99900:09999:09990:00000' }
    method HOUSE          { picture '00900:09990:99999:09990:09090' }
    method TORTOISE       { picture '00000:09990:99999:09090:00000' }
    method BUTTERFLY      { picture '99099:99999:00900:99999:99099' }
    method STICKFIGURE    { picture '00900:99999:00900:09090:90009' }
    method GHOST          { picture '99999:90909:99999:99999:90909' }
    method SWORD          { picture '00900:00900:00900:09990:00900' }
    method GIRAFFE        { picture '99000:09000:09000:09990:09090' }
    method SKULL          { picture '09990:90909:99999:09990:09990' }
    method UMBRELLA       { picture '09990:99999:00900:90900:09900' }
    method SNAKE          { picture '99000:99099:09090:09990:00000' }
    method SCISSORS       { picture '99009:99090:00900:99090:99009' }}
