# Demonstrates: [unused-parameter]  (a NOTE, not a warning — often intentional)
# `$verbose` is threaded through the signature but never consulted; either wire
# it up or drop it.  Run:  rakupp --lint unused-parameter.raku

sub log-line($message, $verbose) {      # <-- $verbose is never used
    say "[log] $message";
}

log-line("started", True);
log-line("done", False);
