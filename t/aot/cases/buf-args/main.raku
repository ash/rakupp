# Buf arguments changed by native bodies (t/aot/run.raku).
use Aot::Buf;

my Buf $b = Buf.new(0 xx 8);
poke($b);
say $b.list;
say set-two($b, 11, 3).list;
say $b.list;
stamp($b, 0xDEADBEEF);
say $b.list;
say own-copy($b), ' ', $b[0];
my $small = Buf.new(1, 2);
$small[5] = 6;
say $small.list;
