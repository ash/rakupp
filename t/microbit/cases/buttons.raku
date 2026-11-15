# stop: 3000
# buttons: A@200,A@700-1000,B@1500,logo@2200-2500
# The buttons: A counts, B shows the count, the logo clears it. get-presses
# and was-pressed count the presses that began since they were last asked.
use MicroBit;

my int $count = 0;
loop {
    $count += button-a.get-presses;
    if button-b.was-pressed {
        say "count $count";
        display.show($count);
    }
    if pin-logo.is-touched {
        $count = 0;
        display.clear;
    }
    say "A is down" if button-a.is-pressed;
    pause 100;
}
