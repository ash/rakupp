# A counter: button A counts up, button B shows the count, and touching the
# logo starts again from 0. A press is counted as it begins, so a quick press
# between two looks is not lost — but while the count scrolls, the program is
# busy, and a touch of the logo then is missed, as on the board.
#
#   RAKUPP_MICROBIT_STOP=3500 RAKUPP_MICROBIT_BUTTONS="A@200,A@600,A@900,B@1500,logo@3100" rakupp buttons.raku
use MicroBit;

my int $count = 0;
loop {
    $count += button-a.get-presses;
    if button-b.was-pressed {
        say "count: $count";
        display.scroll($count);
    }
    if pin-logo.is-touched {
        $count = 0;
        display.show(Image.NO);
    }
    pause 50;
}
