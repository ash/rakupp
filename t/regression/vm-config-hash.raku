# Regression: `$*VM.config` answered the VM's NAME, not its build configuration.
#
# The VM/Distro/Kernel arm ends in a lenient accessor — any method it does not
# know answers the object's `name` — so `$*VM.config` was the Str "moar" and
# `$*VM.config<obj>` was an associative subscript on a Str. That used to give a
# quiet Any, so LibraryMake (the build helper a long tail of NativeCall dists
# shells out to) filled a Makefile template with undefined values and nobody
# noticed; once associative indexing on a DEFINED scalar started dying as
# Rakudo's does, the same line took the dist's whole suite with it — the module
# battery is what caught it, at 0/1 against Rakudo's 1/1.
#
# Fixed by giving `config` an arm of its own: a Hash describing THIS engine's
# toolchain, in the spellings the ecosystem readers expect.
#
# Contract: exit 0 + last line PASS.
my @fail;

my $c = $*VM.config;

# 1. It is a Hash, not the lenient name-string.
@fail.push("config is {$c.^name}, not a Hash") unless $c ~~ Associative;
@fail.push("config stringifies to the VM name") if $c ~~ Str;

# 2. Every key LibraryMake's get-vars reads on the moar branch is present and
#    defined — an absent one reproduces the original quiet-Any failure.
my %v = $c;
for <obj dll cc ccshared ccout cflags ld ldshared ldflags ldlibs ldout ldusr make exe> -> $k {
    @fail.push("config<$k> missing")   unless %v{$k}:exists;
    @fail.push("config<$k> undefined") unless %v{$k}.defined;
}

# 3. The `%s` templates carry their marker: readers substitute or strip it.
#    LibraryMake does `$so ~~ s/^.*\%s//` on <dll> to recover the extension and
#    `$ldusr ~~ s/\%s//` on <ldusr> to recover the bare flag.
@fail.push("config<dll> has no %s template: {%v<dll>.raku}")     unless %v<dll>.contains('%s');
@fail.push("config<ldusr> has no %s template: {%v<ldusr>.raku}") unless %v<ldusr>.contains('%s');

my $so = %v<dll>; $so ~~ s/^.*\%s//;
@fail.push("stripping <dll> left '$so', not an extension") unless $so.starts-with('.');

# 4. The platform spellings agree with the host this is running on.
if $*DISTRO.is-win {
    @fail.push("<obj> is {%v<obj>.raku} on Windows") unless %v<obj> eq '.obj';
    @fail.push("<exe> is {%v<exe>.raku} on Windows") unless %v<exe> eq '.exe';
}
else {
    @fail.push("<obj> is {%v<obj>.raku} on a POSIX host") unless %v<obj> eq '.o';
    @fail.push("<exe> is {%v<exe>.raku} on a POSIX host") unless %v<exe> eq '';
    @fail.push("<dll> is {%v<dll>.raku}") unless %v<dll>.starts-with('lib%s.');
}

# 5. The subscript that started it: reading a key off `$*VM.config` directly
#    must not throw, and must not answer the VM name.
my $obj = $*VM.config<obj>;
@fail.push("\$*VM.config<obj> is {$obj.raku}") unless $obj.defined && $obj ne $*VM.name;

die @fail.join("; ") if @fail;
say "PASS";
