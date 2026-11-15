# stop: 3000
# gestures: 500:shake;520:face up;1800:shake;1820:face up
# seed: 7
# A die: shake the board and it shows a number from 1 to 6. was-gesture
# remembers a shake until it is asked about; randint is the board's own
# random numbers, seeded here so the run repeats.
use MicroBit;

display.show(Image.SQUARE-SMALL);
loop {
    if accelerometer.was-gesture('shake') {
        my int $n = randint(1, 6);
        say "rolled $n";
        display.show($n);
    }
    say "lying face up" if accelerometer.is-gesture('face up') && running-time > 2500;
    pause 50;
}
