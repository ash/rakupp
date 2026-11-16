# Regression: a module `use`d in a bare block keeps seeing the packages it
# loaded itself after that block has ended. `my $S; { use ZB; $S = ZB::S }`
# then `$S.new.m`, where ZB's method names `ZA::Z` from its own `use ZA`, died
# "Could not find symbol '&Z' in 'GLOBAL::ZA'": the block's names are scoped
# to it (requireHidden), and module code is exempt — but the exemption was
# only checked while the block's scope was still alive. Data::Native keeps
# Compress::Zlib::Stream from such a block, and its deflate names
# Compress::Zlib::Raw::z_stream, so every :gzip compress failed on Raku++.
# The program's own `ZB::S` after the block stays refused, as in Rakudo.

use Test;

plan 4;

my $dir = $*TMPDIR.add("block-use-deps-{$*PID}");
$dir.mkdir;
$dir.add('ZA.rakumod').spurt: q:to/END/;
    unit module ZA;
    our class Z is export { method hi { 'Z' } }
    END
$dir.add('ZB.rakumod').spurt: q:to/END/;
    unit module ZB;
    use ZA;
    our class S { method m { ZA::Z.new.hi } }
    END
LEAVE { try .unlink for $dir.dir; try $dir.rmdir }

sub run-code(Str $code) {
    my $p = run $*EXECUTABLE, '-I', $dir.Str, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close).chomp;
    my $err = $p.err.slurp(:close);
    ($out, $err)
}

is run-code(q[my $S; { use ZB; $S = ZB::S }; print $S.new.m])[0], 'Z',
   'after the block, the module still reaches what it loaded itself';
is run-code(q[my $S; if True { use ZB; $S = ZB::S }; print $S.new.m])[0], 'Z',
   'as it did from an if block';
like run-code(q[{ use ZB }; print ZB::S])[1], /'ZB'/,
   'while the program\'s own ZB::S after the block is still refused';
like run-code(q[{ use ZB }; print ZA::Z])[1], /'ZA'/,
   'and so is ZA::Z';
