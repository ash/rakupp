# The Uno R4 WiFi joins a network and prints the address it was given, and
# the LED shows the connection: if the network is lost, the LED goes out and
# the board joins again. The network's name and password are constants, so
# the build reads them, here from the environment, and this file holds
# neither:
#
#   WIFI_SSID=home WIFI_PASS=secret rakupp --target=arduino wifi.raku -o wifi/wifi.ino
#
# The simulated board has a simulated network. RAKUPP_ARDUINO_WIFI="name:password"
# is the one in range; unset, any name and password join:
#
#   WIFI_SSID=home WIFI_PASS=secret RAKUPP_ARDUINO_STOP=3000 rakupp wifi.raku

use Arduino;
use Arduino::WiFi;

constant SSID = %*ENV<WIFI_SSID> // die "set WIFI_SSID to the network's name";
constant PASS = %*ENV<WIFI_PASS> // die "set WIFI_PASS to its password";

pinMode(LED_BUILTIN, OUTPUT);
say "joining {SSID}";
until WiFi.begin(SSID, PASS) == WL_CONNECTED {
    say "not yet: status {WiFi.status}";
    delay(5000);
}
say "joined, address {WiFi.localIP}";

loop {
    my Bool $up = WiFi.status == WL_CONNECTED;
    digitalWrite(LED_BUILTIN, $up);
    unless $up {
        say "lost the network: joining again";
        WiFi.begin(SSID, PASS);
    }
    delay(1000);
}
