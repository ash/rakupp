unit module DirStat::Scan;

#| Every file below $dir, skipping hidden files and directories (.git and co).
sub scan(IO::Path:D $dir --> Seq:D) is export {
    gather for $dir.dir -> $path {
        next if $path.basename.starts-with('.');
        if $path.d {
            take $_ for scan($path);
        }
        elsif $path.f {
            take $path;
        }
    }
}
