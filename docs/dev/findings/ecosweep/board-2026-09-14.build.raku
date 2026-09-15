my $LZ = @*ARGS[0];
my $OUT = @*ARGS[1];
my %row;
sub take($path) {
    return unless $path.IO.e;
    for $path.IO.lines.skip(1) -> $l {
        my @f = $l.split("\t");
        next unless @f[0] && @f[3];
        next if @f[0] eq 'name' | 'dist';
        next if @f[3] eq 'unresolved';
        %row{@f[0]} = $l;
    }
}
take("/Users/ash/raku++/docs/dev/findings/ecosweep/merged-2026-09-12.tsv");
for $LZ.IO.dir.grep(*.basename ~~ /^ [ 'final2' | <-[a..z]>* ] .* '-out.tsv' $/).sort(*.basename) -> $f {
    next if $f.basename eq any <count-out.tsv greens-out.tsv tmo-out.tsv reseed-out.tsv
                                aff-out.tsv parse3-out.tsv aff2-out.tsv agn-out.tsv ft-out.tsv
                                natlib-out.tsv nl2-out.tsv>;
    take($f.absolute);
}
take("$LZ/final2.tsv");
take("$LZ/count-out.tsv");
take("$LZ/greens-out.tsv");
take("$LZ/tmo-out.tsv");
take("$LZ/reseed-out.tsv");
take("$LZ/aff-out.tsv");
take("$LZ/parse3-out.tsv");
take("$LZ/agn-out.tsv");
take("$LZ/aff2-out.tsv");
take("$LZ/ft-out.tsv");
take("$LZ/natlib-out.tsv");
take("$LZ/nl2-out.tsv");
spurt $OUT, "name\tversion\treleased\tverdict\tdetail\tseconds\texit\tfirst-error\n"
          ~ %row.sort(*.key).map(*.value).join("\n") ~ "\n";
note "{%row.elems} rows -> $OUT";
