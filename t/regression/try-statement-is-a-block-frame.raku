# Regression (found by the module battery, Log::Async): `try EXPR` is a
# block of its own in the backtrace, as `try { … }` is and as in Rakudo.
#
# Before the fix the statement form left no frame. Log::Async drops the
# first visible frame as the exception's own (the `try` that threw it) and
# reads the caller's file and line from the next. With no frame for the
# `try` it dropped the caller's, found nothing, and died "No such method
# 'file' for invocant of type 'Any'". That showed once fa865d47 (v5.7.0)
# hid `is hidden-from-backtrace` frames, as Rakudo does. A `try my $x = …`
# still declares $x where it stands.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub frames($e) { $e.backtrace.list.grep({ !.is-setting }).map({ .line ~ ':' ~ (.subname || '-') }).List }

sub a { my $e = Exception.new; try $e.throw; frames $e }
sub b { my $e = Exception.new; try { $e.throw }; frames $e }
check 'try EXPR', a(), ('19:-', '19:a', '21:<unit>');
check 'try { }', b(), ('20:-', '20:b', '22:<unit>');

# Log::Async's shape: the frames that are not hidden, less the try's own
sub ctx is hidden-from-backtrace {
    my $e = Exception.new;
    try $e.throw;
    my @bt = $e.backtrace.grep({ !.is-hidden and !.is-setting });
    @bt.shift;
    @bt[0].line
}
sub caller-of-ctx { ctx() }
check 'the caller is the frame after the try', caller-of-ctx(), 32;

try my $x = 5;
check 'try my $x declares where it stands', $x, 5;
try die 'boom';
check '$! is set', $!.message, 'boom';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
