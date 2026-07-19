# Demonstrates: [unused-variable]
# `$fahrenheit` is computed but never read — probably a copy-paste slip where
# the wrong variable was printed.  Run:  rakupp --lint unused-variable.raku

my $celsius    = 24;
my $fahrenheit = $celsius * 9 / 5 + 32;   # <-- declared, never used
my $kelvin     = $celsius + 273.15;

say "It is $celsius °C ($kelvin K)";
