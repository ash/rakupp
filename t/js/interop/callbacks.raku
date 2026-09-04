# --target=js interop golden: Raku closures handed to JavaScript and JS
# functions handed back.
use JS;
# a JS array crosses by copy: it comes back a Raku Array, with Raku's methods
my $arr = JS.Array.from([1, 2, 3, 4, 5]);
say $arr.WHAT.^name, " ", $arr.elems;
say $arr.map(-> $x { $x * $x }).join(" ");
say $arr.grep(-> $x { $x %% 2 }).join(" ");
# a Raku closure handed to a JS higher-order function is called from JavaScript
my $apply = EVAL '(f, x) => f(x) + 1', :lang<JavaScript>;
say $apply(-> $x { $x * 10 }, 4);
my $fold = EVAL 'arr => arr.reduce((a, b) => a + b, 0)', :lang<JavaScript>;
say $fold([1, 2, 3, 4, 5]);
my $double = EVAL 'x => x * 2', :lang<JavaScript>;
say $double(21);
say [1, 2, 3].map($double);
my &greet = EVAL '(name) => "Hello, " + name', :lang<JavaScript>;
say greet("World");
say JS.Object.keys($arr).elems;
