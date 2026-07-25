# Regression: the 2018.10 command-line protocol (6.c/S06-other/main-refactored.t).
#   RUN-MAIN(&main, $mainline) drives: @*ARGS -> ARGS-TO-CAPTURE -> dispatch &main
#   -> on failure GENERATE-USAGE (or the legacy USAGE) -> exit via &*EXIT.
#   A user sub of any of those names REPLACES the built-in step and is what
#   &*ARGS-TO-CAPTURE / &*GENERATE-USAGE name while it runs. MAIN_HELPER is never
#   called. Two general bugs this surfaced are covered too (slip splicing, =:=).
# Contract: exit 0 + last line PASS.
my @fail;

# --- default parsing ---------------------------------------------------------
my @got;
sub MAIN(*@_, *%_) { @got = [@_.List, %(%_)] }
@*ARGS = <foo>;                RUN-MAIN(&MAIN, Nil);
@fail.push("plain ({@got.raku})")  unless @got[0] eqv ('foo',) && @got[1] eqv {};
@*ARGS = <--bar foo>;          RUN-MAIN(&MAIN, Nil);
@fail.push("named ({@got.raku})")  unless @got[0] eqv ('foo',) && @got[1]<bar> === True;
@*ARGS = <--b=42 -b=666 foo>;  RUN-MAIN(&MAIN, Nil);
@fail.push("repeat ({@got.raku})") unless @got[0] eqv ('foo',) && @got[1]<b>.elems == 2;
# without named-anywhere an option AFTER a positional is positional
@*ARGS = <foo --bar>;          RUN-MAIN(&MAIN, Nil);
@fail.push("basic-after ({@got.raku})") unless @got[0].elems == 2;
{   # …and with it, it binds
    my %*SUB-MAIN-OPTS = named-anywhere => 1;
    @*ARGS = <foo --bar>;      RUN-MAIN(&MAIN, Nil);
    @fail.push("anywhere ({@got.raku})") unless @got[0] eqv ('foo',) && @got[1]<bar> === True;
}

# --- ARGS-TO-CAPTURE replaces the parsing, and sees the right &main -----------
{
    my ($saw-main, $saw-sub);
    sub M2(*@_, *%_) { @got = [@_.List, %(%_)] }
    sub ARGS-TO-CAPTURE(&main, @args) {
        $saw-main = &main =:= &M2;
        $saw-sub  = &*ARGS-TO-CAPTURE ~~ Sub;
        Capture.new(list => ('X',), hash => {y => 1})
    }
    @*ARGS = <ignored>;
    RUN-MAIN(&M2, Nil);
    @fail.push('a2c-main')    unless $saw-main;
    @fail.push('a2c-dynamic') unless $saw-sub;
    @fail.push("a2c-capture ({@got.raku})") unless @got[0] eqv ('X',) && @got[1]<y> == 1;
}

# --- failed dispatch runs GENERATE-USAGE and exits 2 (0 with --help) ----------
{
    my ($exit, $saw-main, $saw-sub, $called);
    my &*EXIT = { $exit = $_ };
    sub M3("NEVER MATCHES") { $called = True }
    sub GENERATE-USAGE(&main, *@_, *%_) {
        $saw-main = &main =:= &M3;
        $saw-sub  = &*GENERATE-USAGE ~~ Sub;
        ''   # empty: nothing printed
    }
    @*ARGS = <foo>;
    RUN-MAIN(&M3, Nil);
    @fail.push('gu-not-called-main') if $called;    # the literal param must NOT match
    @fail.push('gu-main')    unless $saw-main;
    @fail.push('gu-dynamic') unless $saw-sub;
    @fail.push("gu-exit ($exit)") unless $exit == 2;
    @*ARGS = <--help foo>;
    RUN-MAIN(&M3, Nil);
    @fail.push("gu-help-exit ($exit)") unless $exit == 0;
}

# --- the legacy USAGE hook ---------------------------------------------------
{
    my ($exit, $usage-called);
    my &*EXIT = { $exit = $_ };
    sub M4("NEVER MATCHES") { }
    sub USAGE() { $usage-called = True }
    @*ARGS = <foo>;
    RUN-MAIN(&M4, Nil);
    @fail.push('usage-called') unless $usage-called;
}
# --- MAIN_HELPER is never called (it sits alongside ARGS-TO-CAPTURE, as in roast)
{
    my $helper-called;
    sub M5(*@_, *%_) { }
    sub ARGS-TO-CAPTURE(&main, @args) { Capture.new(list => (), hash => {}) }
    sub MAIN_HELPER() { $helper-called = True }
    @*ARGS = <foo>;
    RUN-MAIN(&M5, Nil);
    @fail.push('main-helper-quiet') if $helper-called;
}

# --- the two general bugs this surfaced --------------------------------------
# a slipped array splices its ELEMENTS as-is: an element that is itself an
# empty list survives (flatten() used to swallow it)
my @b = (), 1;
@fail.push("slip-keeps-empty ({(|@b, 2).elems})") unless (|@b, 2).elems == 3;
# `&`-sigil identity is the ROUTINE's, not the container's
sub ident-target() { }
sub takes-code(&x) { &x =:= &ident-target }
@fail.push('code-identity') unless takes-code(&ident-target);
my $held = &ident-target;
@fail.push('scalar-identity') if $held =:= &ident-target;   # a Scalar container: False

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
