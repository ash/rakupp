# Regression (issue #140): a Match's .from/.to cost the same wherever the match
# is. They counted graphemes from the front of the subject on every call, so a
# grammar whose actions read .from on each node — as a compiler does to keep
# source positions — was quadratic in its input, ~30x worse again once one
# non-ASCII character was in it (2,705 lines: 50 s; now each subject's table is
# built once).
#
# First the answers, past the 256-byte prefix that is still counted directly:
# .from must equal the grapheme count of .prematch and .to the count up to the
# end of the match — both counted by .chars, a separate path. Then the cost:
# the same work at 200 and at 1,600 lines, best of three; linear takes about 8x
# as long, quadratic about 64x. A case fails only past 24x AND 100 ms, so a
# busy machine does not turn it red. No timings in the output: the --jit/--cnp
# and --exe gates compare it byte for byte. Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($ok, $desc) {
    if $ok { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc" }
}

# every clustering kind the offset has to step over, then an `x` to find
my @pieces = 'ab', "\r\n", 'é', "e\x[301]\x[302]", '日本', "\x[1F1FA]\x[1F1F8]",
             "\x[1F468]\x[200D]\x[1F469]", "\x[600]a", "\x[915]\x[94D]\x[937]", 'ж';
sub subject($k, $ascii-heavy) {
    my @parts = (^120).map: -> $i {
        $ascii-heavy && $i % 9 ?? 'abc ' !! @pieces[($i * 7 + $k) % @pieces]
    };
    @parts.splice($_, 0, 'x') for 100, 60, 30;
    @parts.join
}
sub offsets-ok($s) {
    so all $s.match(/ x /, :g).map: -> $m {
        $m.from == $m.prematch.chars && $m.to == $m.orig.chars - $m.postmatch.chars
    }
}
ck(offsets-ok(subject(1, False)), '.from/.to on text that clusters everywhere');
ck(offsets-ok(subject(2, True)),  '.from/.to on mostly-ASCII text');
# a CR LF is ONE character (#102), here past the directly-counted prefix
my $crlf = "a\r\n" x 200 ~ 'x';
ck(($crlf ~~ / x /).from == 400, '.from counts each CR LF once');
# more subjects than the cache keeps, asked in turn, twice over
my @subjects = (^7).map({ subject($_, $_ %% 2) });
my @matches = @subjects.map({ .match(/ x /, :g).List });
my $again = all (^2).map: { all @matches.map: { all .map: { .from == .prematch.chars } } };
ck(?$again, '.from stays right across more subjects than the cache holds');
# each thread keeps its own table
my @per-thread = await (^4).map: -> $t { start { offsets-ok(@subjects[$t]) } };
ck(?all(@per-thread), '.from/.to from four threads');

# the cost: actions reading .from/.to on every node, and a :g list's offsets
grammar Src {
    token TOP     { <line>+ }
    token line    { <stmt> ';' \h* <comment>? \n }
    token stmt    { [<word> | <op>]+ % \h+ }
    token op      { <[=+*-]> }
    token word    { \w+ }
    token comment { '#' \N* }
}
class Pos {
    has $.sum = 0;
    method word($/) { $!sum += $/.from + $/.to }
    method op($/)   { $!sum += $/.to }
    method line($/) { $!sum += $/.to }
}
sub source($lines) { (^$lines).map({ "let x$_ = café + naïve$_; # résumé $_\n" }).join }
my %src = 200 => source(200), 1600 => source(1600);
my @cases =
    'grammar actions, .from/.to per node' => -> $n { Src.parse(%src{$n}, :actions(Pos.new)) },
    ':g matches, .from of each'           => -> $n { [+] %src{$n}.match(/ '=' /, :g).map(*.from) };

sub ms(&case, $n) {
    my $best = Inf;
    for ^3 { my $t = now; case($n); $best min= (now - $t) * 1000 }
    $best
}
for @cases -> $c {
    my $name = $c.key; my &case = $c.value;
    my $short = ms(&case, 200);
    my $long  = ms(&case, 1600);
    my $quadratic = $long >= 100 && $long / max($short, 0.01) >= 24;
    ck(!$quadratic, "$name: linear");
    note "  $name: {$short.fmt('%.1f')} ms at 200 lines, {$long.fmt('%.1f')} ms at 1600" if $quadratic;
}

say $fails ?? "FAILED $fails" !! "PASS";
