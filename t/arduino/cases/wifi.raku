# stop: 23500
# wifi: home:correct-horse
# The Uno R4 WiFi's network (use Arduino::WiFi), joined with a name and
# password the build reads from wifi.json beside the program, so neither is
# in its source: a network out of range, then no password, each failure
# waiting out the library's 10 s; then the right one, its address printed,
# and a disconnect the LED shows.
use Arduino;
use Arduino::WiFi;
use JSON::Native;

constant SSID = from-json($?FILE.IO.sibling('wifi.json').slurp)<ssid>;
constant PASS = from-json($?FILE.IO.sibling('wifi.json').slurp)<password>;

pinMode(LED_BUILTIN, OUTPUT);
say "status: ", WiFi.status;
if WiFi.begin("cafe", PASS) != WL_CONNECTED { say "cafe: ", WiFi.status }
if WiFi.begin(SSID) != WL_CONNECTED { say "no password: ", WiFi.status }
until WiFi.begin(SSID, PASS) == WL_CONNECTED { delay(5000) }
say "joined {SSID} at {WiFi.localIP}";

loop {
    state uint8 $n = 0;
    digitalWrite(LED_BUILTIN, WiFi.status == WL_CONNECTED);
    delay(1000);
    $n++;
    if $n == 2 {
        WiFi.disconnect;
        say "status: ", WiFi.status, ", address ", WiFi.localIP;
    }
}
