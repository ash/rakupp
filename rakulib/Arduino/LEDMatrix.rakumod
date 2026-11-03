# Arduino::LEDMatrix, the Uno R4 WiFi's 12×8 LED matrix (ARDUINO-PLAN.md).
# Built into rakupp as a shadow module, like Arduino, so no -I path or store
# stands in for it. Under the interpreter a frame shown is drawn on the
# simulated board's stderr, by the rules the host core of a `--sim` build
# (src/codegen/ArduinoSim.h) draws it with; under `--target=arduino` the
# emitter writes the Arduino_LED_Matrix library's own calls.
#
# A frame is the library's: 96 bits in three 32-bit words, row by row from
# the top left, the most significant bit first. `frame` packs one from a
# picture, while building — `#` (or `*`, `X`, `O`, `1`) lit, `.` dark:
#
#   constant @HEART = frame q:to/END/;
#       ...##...##..
#       ..####.####.
#       …six more rows of twelve
#       END
#
# On the board the constant is three words in flash; `frame` never gets there.

unit module Arduino::LEDMatrix;

need Arduino;

sub frame(Str $picture --> List) is export {
    my @rows = $picture.lines.grep(*.chars);
    die "a frame is 8 rows of 12, not {+@rows} rows" unless @rows == 8;
    my @bits;
    for @rows.kv -> $r, $row {
        die "row {$r + 1} of a frame has {$row.chars} LEDs, not 12" unless $row.chars == 12;
        for $row.comb -> $c {
            if    $c ~~ /<[#*XxOo1@]>/ { @bits.push: 1 }
            elsif $c ~~ /<[.\-_0\ ]>/  { @bits.push: 0 }
            else  { die "row {$r + 1} of a frame has '$c': write # for a lit LED and . for a dark one" }
        }
    }
    (^3).map(-> $w { my $v = 0; $v = $v * 2 + @bits[$w * 32 + $_] for ^32; $v }).List
}

my @shown = 0, 0, 0;   # dark until something is shown

# A frame that changes what the matrix shows is drawn; one that does not is
# not, as a digitalWrite that changes nothing writes nothing.
sub draw(@w) {
    Arduino::advance(1);
    return if @w[0] == @shown[0] && @w[1] == @shown[1] && @w[2] == @shown[2];
    @shown = @w;
    if @w.all == 0 { $*ERR.print: Arduino::stamp() ~ "matrix clear\n"; return }
    my $s = Arduino::stamp() ~ "matrix\n";
    for ^8 -> $r {
        $s ~= '  ';
        for ^12 -> $c {
            my $i = $r * 12 + $c;
            $s ~= (@w[$i div 32] +> (31 - $i % 32)) +& 1 ?? '#' !! '.';
        }
        $s ~= "\n";
    }
    $*ERR.print: $s;
}

class LEDMatrix is export {
    method begin() { Arduino::advance(1) }
    method show(@frame) {
        die "a frame is three 32-bit words, not {+@frame} — make one with `frame`" unless @frame == 3;
        draw(@frame.map(*.Int).List)
    }
    method clear() { draw((0, 0, 0)) }
}
