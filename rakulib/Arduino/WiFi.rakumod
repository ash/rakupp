# Arduino::WiFi, the Uno R4 WiFi's network (ARDUINO-PLAN.md): the WiFiS3
# library that comes with its core. Built into rakupp as a shadow module,
# like Arduino, so no -I path or store stands in for it. Under the
# interpreter the network is simulated, by the rules the host core of a
# `--sim` build (src/codegen/ArduinoSim.h) simulates it with; under
# `--target=arduino` the emitter writes the library's own calls.
#
# The network's name and password are strings the build knows — constants,
# computed while building — so they come from outside the program:
#
#   constant SSID = %*ENV<WIFI_SSID> // die 'set WIFI_SSID';
#   constant PASS = %*ENV<WIFI_PASS> // die 'set WIFI_PASS';
#   until WiFi.begin(SSID, PASS) == WL_CONNECTED { delay(5000) }
#   say WiFi.localIP;
#
# The simulated air: RAKUPP_ARDUINO_WIFI="name:password" is the one network
# in range (empty: none); unset, any name and password join. Joining is
# instant and brings the address, which a board's `begin` waits for (the
# sketch's rk_wifi_joined); a failed one takes the library's 10 s timeout.
# Each attempt, and a disconnect, is a line on stderr — never the password.
# The network's time, `WiFi.getTime`: RAKUPP_ARDUINO_TIME=SECONDS (since 1970,
# UTC) is the time when the board starts, unset this computer's clock, and the
# board's own time runs on from it; 0 until the board has joined.

unit module Arduino::WiFi;

need Arduino;

# wl_status_t of WiFiS3's WiFiTypes.h
constant WL_NO_MODULE       is export = 255;
constant WL_IDLE_STATUS     is export = 0;
constant WL_NO_SSID_AVAIL   is export = 1;
constant WL_CONNECTED       is export = 3;
constant WL_CONNECT_FAILED  is export = 4;
constant WL_CONNECTION_LOST is export = 5;
constant WL_DISCONNECTED    is export = 6;

constant IP = '192.168.1.42';   # the simulated network's address for the board
constant TIMEOUT-MS = 10_000;   # CWifi::_timeout

my $air = %*ENV<RAKUPP_ARDUINO_WIFI>;
my $epoch = do with %*ENV<RAKUPP_ARDUINO_TIME> { .Int if .chars } // time;
my $status = WL_IDLE_STATUS;
my $joined = '';

sub report(Str $s) { $*ERR.print: Arduino::stamp() ~ "wifi $s\n" }

class WiFi is export {
    method begin(Str() $ssid, Str() $pass = '') {
        Arduino::advance(1);
        my $why = WL_CONNECTED;
        with $air {
            my ($name, $key) = .split(':', 2);
            $why = !.chars || $ssid ne $name ?? WL_NO_SSID_AVAIL
                !! $pass ne ($key // '') ?? WL_CONNECT_FAILED !! WL_CONNECTED;
        }
        if $why == WL_CONNECTED {
            ($status, $joined) = WL_CONNECTED, $ssid;
            report "\"$ssid\" connected, ip {IP}";
            return WL_CONNECTED;
        }
        Arduino::delay(TIMEOUT-MS);
        ($status, $joined) = $why, '';
        report "\"$ssid\": " ~ ($why == WL_NO_SSID_AVAIL ?? 'no such network' !! 'wrong password');
        WL_CONNECT_FAILED
    }
    method status()  { Arduino::advance(1); $status }
    method localIP() { Arduino::advance(1); $status == WL_CONNECTED ?? IP !! '0.0.0.0' }
    method getTime() {
        Arduino::advance(1);
        $status == WL_CONNECTED ?? $epoch + Arduino::clock-us() div 1_000_000 !! 0
    }
    method disconnect() {
        Arduino::advance(1);
        return 1 unless $status == WL_CONNECTED;
        report "\"$joined\" disconnected";
        ($status, $joined) = WL_DISCONNECTED, '';
        1
    }
}
