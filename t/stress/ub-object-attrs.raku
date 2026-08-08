# CONTRACT test: N workers mutate the same object's attributes — scalar,
# array and hash attrs, plus method-mediated writes — with no lock. The
# attribute VALUES are undefined behavior; the object system must survive.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 1500).Int;
class Shared {
    has $.count is rw = 0;
    has @.log;
    has %.tags;
    method bump($i) { $!count = $!count + 1; @!log.push($i); %!tags{"t" ~ ($i % 31)}++ }
}
my $obj = Shared.new;
await (^$N).map: -> $w {
    start {
        for ^$M -> $i {
            $obj.bump($w * $M + $i);
            $obj.count = $obj.count + 1;
        }
    }
};
say "survived: count={$obj.count} log={$obj.log.elems} tags={$obj.tags.keys.elems} (values are UB; dying is not)";
say 'PASS';
