# MAIN's parameters: positional, defaulted and named, all bare.
sub MAIN(name, times = 2, :shout) {
    my line = "hi {name}";
    line = line.uc if shout;
    say line for ^times;
}
