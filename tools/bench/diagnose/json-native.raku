# The same documents as json-parse.raku, parsed AND serialised through
# Rakupp::JSON — the native extension module — so the three configurations that
# matter are comparable on one corpus:
#
#   Rakudo + JSON::Fast        the bar
#   rakupp + JSON::Fast        json-parse.raku — the interpreter's own speed
#   rakupp + Rakupp::JSON      this file — the C parser behind the extension ABI
#
# It PRINTS THE BACKEND IT USED on every line, which is the whole point of the
# design. Rakupp::JSON falls back to JSON::Fast when the extension is missing,
# on Rakudo, or after a compiler upgrade the module has not been rebuilt for —
# so a run that quietly measured the fallback would otherwise look like a
# catastrophic regression, and a run on Rakudo would look like a triumph. The
# label makes the measurement honest without anyone having to remember.
#
# Runs unchanged under both engines, like everything else in this directory.
#
#     M=~/raku-modules/Rakupp-JSON        # the module, with its library built
#     rakupp -I$M/lib json-native.raku --reps=5 d200.json d400.json d800.json d1600.json
#     raku   -I$M/lib json-native.raku --reps=5 d200.json d400.json d800.json d1600.json
#
# Sub-millisecond parses are the normal case here, so this reports microsecond
# resolution where json-parse.raku's whole milliseconds would print "0 ms".
use Rakupp::JSON;

sub MAIN(*@files, Int :$reps = 3) {
    my $backend = json-backend();
    say "backend: $backend   ({ $*RAKU.compiler.name })";
    for @files -> $f {
        my $text = $f.IO.slurp;
        my (@parse, @write);
        my $data;
        for ^$reps {
            my $t0 = now;
            $data = from-json($text);
            @parse.push: ((now - $t0) * 1000).Num;
            die "bad parse of $f" unless $data.elems;   # never time a no-op

            my $t1 = now;
            my $out = to-json($data, :!pretty);
            @write.push: ((now - $t1) * 1000).Num;
            die "bad write of $f" unless $out.chars;
        }
        report($f, $text.chars, 'parse', @parse);
        report($f, $text.chars, 'write', @write);
    }
}

sub report($f, $chars, $what, @t) {
    my $best = @t.min;
    my $mb   = ($chars / 1024 / 1024) / ($best / 1000);
    # .fmt, not .round: these are Nums off a clock, and rounding one prints
    # 59.800000000000004 rather than 59.8.
    say "$f\t$what\t{ $chars } chars\t{ $best.fmt('%.3f') } ms\t{ $mb.fmt('%.1f') } MB/s"
        ~ "  (runs: { @t.map(*.fmt('%.3f')).join(',') })";
}
