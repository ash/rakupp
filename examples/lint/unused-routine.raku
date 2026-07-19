# Demonstrates: [unused-routine]
# `celsius-to-kelvin` was refactored out but its call site was deleted, leaving
# a lexical routine nothing reaches.  Run:  rakupp --lint unused-routine.raku

sub celsius-to-fahrenheit($c) { $c * 9 / 5 + 32 }

sub celsius-to-kelvin($c) { $c + 273.15 }        # <-- never called

say celsius-to-fahrenheit(100);
