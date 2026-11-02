# stop: 1000
# pins: 2=0,A0=512
# Inputs fixed from outside (RAKUPP_ARDUINO_PINS), Serial.println's CR LF,
# and note on the error stream.
use Arduino;

constant BUTTON = 2;
constant @levels = 10, 20, 30;
pinMode(BUTTON, INPUT_PULLUP);
pinMode(3, INPUT);
Serial.begin(115200);
Serial.println("button=", digitalRead(BUTTON), " pin3=", digitalRead(3), " A0=", analogRead(A0));
note "a note goes to the error stream";
my int $i = 0;
loop {
    my int $v = analogRead(A0) div 4;
    analogWrite(9, $v);
    Serial.print("level ");
    Serial.println(@levels[$i % @levels.elems]);
    $i++;
    delay(100);
}
