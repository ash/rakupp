# Native bodies entered from several threads at once: each call has its own
# frame, outer names are bound per call, and module state is shared.
unit module Aot::Threads;

my $lock = Lock.new;
my %tally;

sub work(Int $id, Int $n) is export {   #aot: native
    my $acc = '';
    for ^$n -> $i {
        $acc ~= ($id + $i) % 10;
    }
    $lock.protect({ %tally{$id % 3} += $n });
    "$id:$acc"
}
sub tally() is export {   #aot: native
    %tally.sort(*.key).map({ "{.key}={.value}" }).join(' ')
}
# a native body that starts threads itself and awaits them
sub fan-out(@ids) is export {   #aot: native
    my @promises = @ids.map(-> $id { start { work($id, 4) } });
    await(@promises).sort.join(' ')
}
class Worker is export {
    has $.label;
    has @.done;
    has $!lock = Lock.new;
    method run(Int $n) {   #aot: native
        my $r = "{$!label}{$n}";
        $!lock.protect({ @!done.push($r) });
        $r
    }
    method finished() {   #aot: native
        @!done.sort.join(',')
    }
}
