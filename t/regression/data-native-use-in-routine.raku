# A `use` of a module the engine answers, written inside a routine body.
#
# Since 1099187e the `use`s inside routine bodies are loaded when the routine's
# declaration is reached, importing nothing. The engine answers Data::Native
# and its siblings only for an importing `use`, so that preload went looking
# for a Data::Native file, and without the distribution installed the program
# died "Could not find Data::Native" before running. v5.4.0 shipped it; the
# spreadsheet formulas (bindings/spreadsheets/rakusheet.raku) were the first
# to say so, in the tag's wasm job, where nothing is installed.
#
# The engine answers the `use` when it runs. The cases below need nothing
# installed; Digest::Native has no distribution on any machine this suite
# runs on, and Data::Native is the module that program uses.

use Test;

sub json-in-sub {
    use Data::Native <json>;
    to-json([1, 2], :!pretty)
}
is json-in-sub(), '[1,2]', 'use Data::Native <json> inside a sub';

sub all-tags-in-sub {
    use Data::Native;
    md5-hex('abc')
}
is all-tags-in-sub(), '900150983cd24fb0d6963f7d28e17f72', 'a bare use Data::Native inside a sub';

class C {
    method digest {
        use Digest::Native;
        sha1-hex('abc')
    }
}
is C.new.digest, 'a9993e364706816aba3e25717850c26c9cd0d89d', 'use Digest::Native inside a method';

my &later = sub {
    use Data::Native <json>;
    from-json('{"a":1}')<a>
};
is later(), 1, 'use Data::Native inside an anonymous sub';

done-testing;
say 'PASS';
