# rakulang — Raku from Python

`rakulang` is based on [Raku++](https://github.com/ash/rakupp), an
implementation of the Raku language written in C++. The Raku++ engine comes
inside the package.

Run Raku code and Raku grammars inside a Python program. Raku is good at
text: its grammars turn messy input into structured data, and its numbers are
exact (integers of any size, rationals). `rakulang` lets a Python program use
that without leaving Python.

## Install

```bash
pip install rakulang
```

That is all. You need Python 3.9 or later on macOS, Linux or Windows. There is
nothing to compile and nothing else to install: the package carries its own
Raku engine.

If pip answers `error: externally-managed-environment` (Ubuntu, Debian and
Homebrew's Python do), your system Python does not take packages directly.
Make a virtual environment for your project and install into that:

```bash
python3 -m venv ~/raku-env
```
```bash
~/raku-env/bin/pip install rakulang
```

Then run your programs with `~/raku-env/bin/python`, or run
`source ~/raku-env/bin/activate` once per terminal so that plain `python3`
and `pip` use it. On Ubuntu and Debian, `python3 -m venv` needs
`sudo apt install python3-venv` the first time.

If you installed `rakulang` before, upgrade it to get the newest engine.
`pip install` alone keeps a version that is already there:

```bash
pip install --upgrade rakulang
```

## Your first program

Save this as `hello.py`:

```python
import rakulang

raku = rakulang.interpreter()

raku.eval('say "Hello from Raku!"')

print(raku.eval("(1..10).sum"))
print(raku.eval("2 ** 100"))
print(raku.eval("<apple banana cherry>.map(*.uc)"))
```

Run it with `python3 hello.py`:

```
Hello from Raku!
55
1267650600228229401496703205376
['APPLE', 'BANANA', 'CHERRY']
```

`rakulang.interpreter()` gives you the Raku interpreter. `raku.eval(code)` runs
a piece of Raku code. Raku's `say` prints straight to your terminal, as the
first line shows. `eval` also returns the code's result as an ordinary Python
value (a number, a string, a list, a dict), which the other lines print from
Python.

## Variables live on between calls

Everything you declare stays in the interpreter, so later calls can use it:

```python
import rakulang

raku = rakulang.interpreter()

raku.eval("my @words = <the quick brown fox>")
raku.eval("say @words.elems")
raku.eval("say @words.grep(*.chars > 3)")

raku.eval("my %age = Ada => 36, Alan => 41")
raku.eval("say %age<Ada>")
raku.eval("say %age")
```

```
4
(quick brown)
36
{Ada => 36, Alan => 41}
```

This time Raku does the printing, so you see Raku's own notation: `(quick
brown)` for a list and `{Ada => 36, Alan => 41}` for a hash. Printed from
Python, the same values would look like `['quick', 'brown']` and
`{'Ada': 36, 'Alan': 41}`.

## Your own operators

Raku lets you define new operators. Here is factorial, written the way it is
in a maths book, as `!` after the number:

```python
import rakulang

raku = rakulang.interpreter()

raku.eval("sub postfix:<!>($n) { [*] 1..$n }")

raku.eval("say 5!")
raku.eval("say (1..10).map({ $_! })")

print(raku.eval("50!"))
```

```
120
(1 2 6 24 120 720 5040 40320 362880 3628800)
30414093201713378043612608166064768844377641568960512000000000000
```

`postfix:<!>` declares an operator that goes after its operand, and
`[*] 1..$n` multiplies all the numbers from 1 to `$n`. Once defined, `!` works
in every later `eval`. Raku integers have no size limit, and `50!` arrives in
Python as an ordinary `int`, with all 65 digits.

## Calling Raku subs from Python

Define subs with `eval`, then call them by name with `raku.call`. Python
values go in as arguments, and the result comes back as a Python value:

```python
import rakulang

raku = rakulang.interpreter()

raku.eval("""
    sub area($w, $h)   { $w * $h }
    sub total(@prices) { @prices.sum }
    sub describe(%p)   { "%p<name> costs %p<price>" }
    sub hello($name, $greeting = 'Hello') { "$greeting, $name!" }
""")

print(raku.call("area", 3, 4))
print(raku.call("total", [1, 2, 3.5]))
print(raku.call("describe", {"name": "tea", "price": 3}))
print(raku.call("hello", "Ada"))
print(raku.call("hello", "Ada", "Hi"))
```

```
12
6.5
tea costs 3
Hello, Ada!
Hi, Ada!
```

A Python list arrives in Raku as an array (`@prices`), and a dict arrives as
a hash (`%p`).

If your Raku code is in a file, load all of it at once:

```python
raku.eval(open("tools.raku").read())
```

Keyword arguments become Raku's **named** arguments:

```python
raku.eval('sub greet(:$name, :$age = 0) { "Hello, $name! You are $age." }')
print(raku.call("greet", name="Ada", age=36))
```

```
Hello, Ada! You are 36.
```

## Using Raku modules

`raku.use("Name")` loads a Raku module, as `use Name;` does in a Raku
program, and gives it to you as a Python object. The subs and classes the
module exports are its attributes, and the objects you get from them have
their Raku methods.

Save this module as `lib/Geo.rakumod`:

```raku
unit module Geo;

class Point is export {
    has $.x;
    has $.y;

    method distance-to(Point $other) {
        sqrt(($!x - $other.x)² + ($!y - $other.y)²)
    }

    method moved(:$dx = 0, :$dy = 0) {
        Point.new(x => $!x + $dx, y => $!y + $dy)
    }

    method Str { "($!x, $!y)" }
}

sub origin() is export { Point.new(x => 0, y => 0) }

sub farthest(@points) is export {
    @points.max(*.distance-to(origin()))
}
```

Next to the `lib` folder, save `geo.py`:

```python
import rakulang

raku = rakulang.interpreter()

geo = raku.use("Geo")

p = geo.Point.new(x=3, y=4)
print(p)
print(p.x, p.y)
print(p.distance_to(geo.origin()))

q = p.moved(dx=10)
print(q)

points = [p, q, geo.Point.new(x=-20, y=1)]
print(geo.farthest(points))
```

Run `python3 geo.py` in that folder:

```
(3, 4)
3 4
5.0
(13, 4)
(-20, 1)
```

How it reads:

- `raku.use("Geo")` finds `lib/Geo.rakumod` because Raku looks in the
  `lib` folder of the folder you run Python in. The next section says how
  to keep modules somewhere else.
- `geo.Point` is the class, and `geo.Point.new(x=3, y=4)` is Raku's
  `Point.new(x => 3, y => 4)`: keyword arguments are named arguments.
- `p.x` reads the attribute that `has $.x` declares. Everything else is a
  method, called with parentheses: `p.moved(dx=10)`.
- A Python name cannot contain a hyphen, so write an underscore instead:
  `p.distance_to` calls `distance-to`.
- Raku objects can go back into Raku: `geo.farthest(points)` receives a
  Python list of three `Point`s.
- `print(p)` prints the object's Raku `Str`.

A number, a string, a list or a dict comes back as a plain Python value, as
it does from `eval`. Anything else, such as a `Point`, comes back as a
`rakulang.Object` that stays alive inside the interpreter for as long as
Python holds it.

After `raku.use("Geo")`, the module's exports work in `eval` too:
`raku.eval("origin().x")` gives `0`.

### Where Raku looks for modules

Without being told, Raku looks for a module in `lib/` and in `.` of the
folder you run Python in, and then among the modules installed on your
machine. An installed module needs nothing more: `raku.use("JSON::Fast")`
finds it where Raku++ finds it.

To keep your modules in another folder, name it in the `RAKULIB`
environment variable when you run the program:

```bash
RAKULIB=/home/ada/raku-modules python3 geo.py
```

Separate several folders with commas. A relative folder is taken from the
folder you run Python in, so an absolute path is the safer choice. In
Windows PowerShell, set the variable first with
`$env:RAKULIB = "C:\raku-modules"`, then run `python geo.py`.

A program can also name the folders itself, with `raku.lib`:

```python
raku.lib("/home/ada/raku-modules")
geo = raku.use("Geo")
```

`raku.lib` takes one folder, several (`raku.lib("lib", "vendor")`), or a
list of them (`raku.lib(["lib", "vendor"])`), as strings or `pathlib.Path`s.
The folders are searched in the order you give them, and before any folder
added by an earlier `raku.lib`. A relative folder is taken from the folder
Python is in when `raku.lib` runs. Call it before the `raku.use` that needs
it.

`raku.lib(...)` does what `use lib` does in a Raku program, and
`raku.eval("use lib '/home/ada/raku-modules'")` works as well. The one
difference is the order: within one `use lib 'a', 'b'` line, Raku searches
`b` first.

Setting `os.environ["RAKULIB"]` from Python works only before the first
`rakulang.interpreter()` call, because the interpreter reads the variable
when it starts. `raku.lib` works at any time.

### Classes you define with eval

`raku.main` reaches everything declared by `eval` in the same way:

```python
import rakulang

raku = rakulang.interpreter()

raku.eval("""
    class Counter {
        has $.count = 0;
        method bump(:$by = 1) { $!count += $by; self }
    }
""")

c = raku.main.Counter.new()
c.bump()
c.bump(by=10)
print(c.count)
```

```
11
```

## Parsing text with a grammar

A grammar describes the shape of some text. Here is one for a shopping list
of `name=quantity` pairs, with an actions class that adds up the quantities
while it parses:

```python
import rakulang

source = """
grammar Shopping {
    rule  TOP  { <item>+ }
    rule  item { <name> '=' <qty> }
    token name { \\w+ }
    token qty  { \\d+ }
}

class ShoppingActions {
    method item($/) { make $<qty>.Int }
    method TOP($/)  { make $<item>.map(*.made).sum }
}
"""

shopping = rakulang.Grammar.from_source(source, name="Shopping",
                                        actions="ShoppingActions")

m = shopping.parse("milk=2  bread = 1\neggs=12")

for item in m["item"]:
    print(item["name"].str(), item["qty"].int())

print("total:", m.made)
print(m.tree())
print(shopping.parse("milk=lots"))
```

```
milk 2
bread 1
eggs 12
total: 15
{'item': [{'name': 'milk', 'qty': '2'}, {'name': 'bread', 'qty': '1'}, {'name': 'eggs', 'qty': '12'}]}
None
```

What you can do with the result of `parse`:

- `m["item"]` picks the captures named `item`, and you can loop over them.
- `.str()` gives a capture's text and `.int()` gives it as a number.
- `m.made` is whatever the actions class computed with `make`.
- `m.tree()` turns the whole result into plain Python lists and dicts. The
  leaves in it are strings, so use `.int()` or an actions class when you want
  numbers.
- `parse` returns `None` when the text does not match.

Inside a normal Python string, write `\\w` and `\\d` so that Raku receives
`\w` and `\d`.

### Keeping the grammar in its own file

A grammar is easier to write and read in a file of its own: no escaping, and
your editor highlights it as Raku. Save the same grammar and actions as
`shopping.raku`:

```raku
grammar Shopping {
    rule  TOP  { <item>+ }
    rule  item { <name> '=' <qty> }
    token name { \w+ }
    token qty  { \d+ }
}

class ShoppingActions {
    method item($/) { make $<qty>.Int }
    method TOP($/)  { make $<item>.map(*.made).sum }
}
```

Next to it, save `shop.py`:

```python
import rakulang

shopping = rakulang.Grammar.from_file("shopping.raku", name="Shopping",
                                      actions="ShoppingActions")

m = shopping.parse("milk=2  bread = 1\neggs=12")

for item in m["item"]:
    print(item["name"].str(), item["qty"].int())

print("total:", m.made)
```

Run `python3 shop.py` in that folder:

```
milk 2
bread 1
eggs 12
total: 15
```

`from_file` takes the same arguments as `from_source`: `name` is the grammar
to use, and `actions` is the actions class, both from the file. The path is
relative to the folder you run Python in.

## When something goes wrong

A Raku error (`die`, a failed call) arrives in Python as
`rakulang.RakuError`, carrying Raku's message:

```python
import rakulang

raku = rakulang.interpreter()

try:
    raku.eval('die "out of coffee"')
except rakulang.RakuError as e:
    print("Raku said:", e)

raku.eval("sub area($w, $h) { $w * $h }")
try:
    raku.call("area", 3)
except rakulang.RakuError as e:
    print("Raku said:", e)
```

```
Raku said: out of coffee
Raku said: Calling area(Int) will never work with declared signature ($w, $h)
```

To find out *where* a parse failed, pass `strict=True`. Instead of returning
`None`, `parse` then raises `rakulang.ParseError` with the line, the column
and the rule it was trying:

```python
try:
    shopping.parse("milk=2\nbread=lots", strict=True)
except rakulang.ParseError as e:
    print(f"line {e.line}, column {e.column}, while trying <{e.rule}>")
```

```
line 2, column 7, while trying <qty>
```

## How values convert

| Raku | Python |
|---|---|
| `Int` (any size) | `int` |
| `Num`, `Rat` | `float` |
| `Str` | `str` |
| `True`, `False` | `bool` |
| `List`, `Array` | `list` |
| `Hash` | `dict` |
| `Any` (no value) | `None` |
| anything else | from `eval` and `call`: a `str`; from a module or an object: a `rakulang.Object` |

Arguments convert the same way in reverse: `None`, `bool`, `int`, `float`,
`str`, `list`, `tuple`, `dict` and `rakulang.Object` are accepted. Any other
Python type raises `TypeError`.

---

## For experienced users

Everything below is optional. The sections above are all a typical program
needs.

### How it works

The package is plain Python over the C interface of `librakupp`, the library
form of the [Raku++](https://raku.online) engine. It loads the library with
`ctypes`, from the standard library, so there is no compiled glue. Values
cross through
[`rakupp.h`](https://github.com/ash/rakupp/blob/main/include/rakupp/rakupp.h).
The grammar support is a small Raku shim (`rakulang/grammar_shim.raku`) that
the package evaluates into the interpreter at startup. Modules and objects
have one of their own (`rakulang/object_shim.raku`), evaluated the first time
a program uses `use`, `main`, an object, or a keyword argument to `call`.

The package is named for the language, in the Raku community's disambiguated
spelling: `raku` is an unrelated package on PyPI. Write
`import rakulang as raku` if you prefer the short name. The package version is
the version of the engine inside it; `rakulang.interpreter().version` reports
it.

Wheels are built for macOS 11 or later (universal: Apple silicon and Intel),
Linux with glibc 2.28 or later (x86_64 and aarch64) and Windows x64. On any
other platform pip reports that it finds no matching distribution, and you
build the library yourself (see below).

### More about calls

`call` checks its arguments exactly as a call written in Raku is checked. Too
few or too many arguments, or a value that fails a type constraint, raise
`RakuError` instead of binding silently. The conversion is by Python type, so
`sub flag(Bool $b)` wants `True`, not `1`.

A dict is one positional `Hash`; keyword arguments are the named ones.
`multi` subs, slurpy `*@args`, and `our sub`s inside a package
(`raku.call("Geo::perimeter", 3, 4)`) all resolve through `call`.
`raku.can("area")` says whether a sub of that name is callable. `call`
returns plain Python data, as `eval` does; to get objects back, reach the
sub through `raku.main` or a module instead (`raku.main.area(3, 4)`).

`call` sees the subs declared in the mainline scope and imported into it.
The core's own subs (`sprintf`, `sqrt`) are not among them; `raku.main.sprintf`
reaches them.

An integer wider than 64 bits arrives as an ordinary Python `int`: the C
interface passes integers as `int64`, so the binding reads the digits instead
whenever that saturates.

### More about modules and objects

`raku.use(spec)` takes what follows `use` in Raku, import arguments
included: `raku.use("Geo :ALL")`, `raku.use("JSON::Fast:ver<0.19+>")`. It
runs the `use` in the mainline scope, so `eval` sees the exports afterwards,
and again inside a scope of the module handle's own, so the handle answers
for exactly what that import brought in. Loading the same module twice is
cheap: the module compiles once.

A module handle's attributes are looked up in this order: the symbols the
import brought in (exported subs, classes, constants, enum values), then the
module's package (`our sub`s and nested classes: `geo.perimeter`), then,
when the module is itself a class, that class's methods
(`raku.use("Cro::HTTP::Client").new()`). `dir(geo)` lists the names, and a
name that is none of them raises `AttributeError`. `raku.main` looks names
up in the mainline scope instead, where the core's subs and types are
visible too: `raku.main.Date.new(2026, 10, 5)`.

On a `rakulang.Object`:

- **Names.** `obj.name` tries the name as written, then with each `_` as
  `-`. `getattr(obj, "to-json")` spells a name exactly. Keyword arguments
  follow the same rule against the routine's signature: a name the signature
  declares as written is passed as written, any other is dashed
  (`sorted_keys=True` is `:sorted-keys`).
- **Attributes and methods.** A public attribute (`has $.x`) reads as a value,
  and an `is rw` one can be assigned: `p.label = "home"`. Anything else is a
  `rakulang.Method` to call, even with no arguments: `circle.area()`. For
  the core types, the public attributes are those Rakudo declares: a `Pair`'s
  `key` and `value`, a `Range`'s `min` and `max`, a `Date`'s `year`,
  `month` and `day`, a `Complex`'s `re` and `im`, a `Rat`'s `numerator` and
  `denominator`, an enum value's `key` and `value`.
- **Python protocols.** `str()` is the Raku `.Str` (`.gist` for a type
  object), `repr()` shows `.raku`, `int()`, `float()`, `bool()` and `len()`
  are `.Int`, `.Num`, `.Bool` and `.elems`. `obj[2]` and `obj["key"]` are
  Raku subscripts (`obj[-1]` is `obj[*-1]`). `for x in obj` iterates one value
  at a time, so a lazy or infinite sequence works. `==` is `eqv`, and objects
  hash by `.WHICH`, so enum values make dict keys. Calling an object calls it:
  `geo.origin` is the `Sub`, and `geo.origin()` its result.
- **Results.** A list or a dict that comes back is a Python copy, with any
  objects inside it as `rakulang.Object`s. A lazy sequence comes back as an
  object, to iterate. A `Failure` is raised as `RakuError`.
- **Cost.** Every attribute read and every method call is one call into the
  engine. A loop over a million objects is better written in Raku and called
  once.
- **Lifetime.** An object holds its Raku value alive until Python collects
  it. `obj.close()` lets go of it at once.

`eval` and a plain `call` still convert everything to Python data: an object
comes back as a string. The object surface is loaded the first time a
program uses it.

### More about grammars

`from_file(path, name=..., actions=...)` and `from_source` compile and cache:
identical source compiles once. Each *named* compile is isolated in its own
wrapper package, so recompiling an edited grammar under the same name works,
and earlier handles keep the body they were compiled from. Without `name`
there is no wrapper, and a same-name recompile raises the engine's
`X::Redeclaration`. `name` may be omitted only when the grammar declaration is
the source's last statement.

`parse(text, rule=...)` anchors to the whole input. Pass `rule=` to parse a
fragment with one rule. Indexing a match builds a lazy path, and nothing
crosses into the engine until a terminal operation: `.str()`, `.int()`,
`.num()`, `.made`, `bool()`, `len()`, iteration, `.tree()`, or `.match()`,
which returns an independent `Match`. A lazy walk costs one engine call per
leaf; `.tree()` converts everything at once, which takes about ten times as
long as the parse itself.

A terminal operation on a missing capture raises `RakuError`; `bool()` and
`len()` answer `False` and `0` instead, which is how you test for one.

### Lifetime, threads and output

There is one interpreter per process, created on first use. One Python thread
talks to it at a time; Raku code inside it may start threads of its own. A
`Match` holds values alive inside the interpreter: `close()` it, use it in a
`with` block, or let the garbage collector release it. A `rakulang.Object`
does the same and has `close()` too. Values returned by `eval` and `call` are
plain Python data and need nothing.

Raku's `say` and `print` write to the same standard output as Python, through
their own buffer. On a terminal the lines come out in order. When the output
is piped or redirected, Python holds its own lines back until its buffer
fills, so a `say` that runs after a `print` may appear before it. Run Python
with `-u`, or print with `flush=True`, if the order matters.

### Using your own build of the engine

This is for working on the binding itself, or for a platform with no wheel.
Build `librakupp` at the root of a [rakupp](https://github.com/ash/rakupp)
checkout:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
```
```bash
cmake --build build -j
```

A build directory configured without `-DRAKUPP_BUILD_SHARED=ON` is
static-only, and this package cannot use it. Then install the package from the
checkout with `pip install -e bindings/python`.

Without a bundled library, the package usually finds one by itself: if
`rakupp` is on PATH, it takes `librakupp` from beside it (an installed
layout's sibling `lib/`, a Homebrew keg's, or the build directory the binary
sits in). To choose one explicitly, pass a path to
`rakulang.interpreter("/path/to/librakupp.dylib")`, or set `RAKUPP_LIB` (the
file) or `RAKUPP_HOME` (an install prefix with `lib/`).

**A library you name is used as given.** If it cannot be loaded, you get that
error, not a quiet fall-back to some other library that happens to be
findable. The usual cause is an architecture mismatch, and a fall-back would
make the symptom (another build's behaviour) point nowhere near the cause.
Unset the variable to search instead.

The full search order: the path passed to `interpreter()`, then `RAKUPP_LIB`,
then `RAKUPP_HOME/lib/`; otherwise a copy bundled inside the package
(`rakulang/_lib/`), then the library beside the `rakupp` on PATH (its sibling
`lib/`, then its own directory), then the system linker path.

On ELF platforms the library is loaded `RTLD_GLOBAL`, so Raku extensions that
are `dlopen`ed later can resolve `rk_*` (a requirement from ABI-PLAN A3).

The repository's two examples run from the repo root against such a build
(`.so` for `.dylib` on Linux):

```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/python/examples/calc.py
```
```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/python/examples/shopping.py
```

Their expected output is in
[bindings/examples/expected/](https://github.com/ash/rakupp/tree/main/bindings/examples/expected).

### Building a wheel

`tools/build-wheel.sh <build-dir>` bundles that build's library into the
package. A `pip install` of the result needs no `rakupp` on PATH and no
variables. The bundled copy is a snapshot: `.version` reports it, and
refreshing it means rebuilding and reinstalling. The script builds in a
scratch venv, so it needs pip access to PyPI.

```bash
tools/build-wheel.sh build dist-wheel
```
```bash
python3 -m pip install --force-reinstall --no-deps dist-wheel/rakulang-*.whl
```

On Windows, run it under Git Bash and name the configuration directory the
Visual Studio generator writes to, `build/Release`, where `librakupp.dll` is.

### Testing

```bash
build/rakupp tools/bindings-smoke.raku
```

This runs both examples in every binding's language and checks the output
against
[bindings/examples/expected/](https://github.com/ash/rakupp/tree/main/bindings/examples/expected),
then runs this binding's own tests of modules and objects
(`bindings/python/tests/`). Those also run on their own from the repository
root:

```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib python3 -m unittest discover -s bindings/python/tests
```
For the deep gate (this binding driving the same grammar and 2000-line corpus
as the Raku reference driver, byte-compared), run
`build/rakupp tools/grammar-smoke.raku`. Both run in CI on every push.

### Troubleshooting

Four questions settle most reports: which Python ran, which copy of the
package it imported, which library file that copy loaded, and which engine
that library is. One line answers all four:

```bash
python3 -c "import rakulang, sys; r = rakulang.interpreter(); print(sys.executable, rakulang.__file__, r._lib._name, r.version, sep='\n')"
```

`_lib._name` is the loaded file's path (a private attribute, fine for
diagnosis). With a wheel installed, it points inside the package's own
`_lib/` directory.

**`librakupp not found`.** Nothing bundled, nothing beside `rakupp`, nothing
on the linker path; the message lists every path it tried. If it continues
`A rakupp binary WAS found (…) but its build carries no shared library`, the
build directory on PATH is configured without `-DRAKUPP_BUILD_SHARED=ON`.
Reconfigure it with that option and build again, set `RAKUPP_LIB` to a build
that has the library, or `pip install rakulang` for the bundled one.

**`RAKUPP_LIB names …, which could not be loaded`.** A named library is
authoritative; the loader does not fall back to another. The quoted `dlopen`
error says why: `no such file` when the path does not exist (a relative path
is resolved against the current directory, so a shell profile wants an
absolute one), or the architecture mismatch below. Unset the variable to
search instead.

**`incompatible architecture`.** Your `python3` and the library disagree
(`file $(which python3)` against `file build/librakupp.dylib`). Build the
library for your interpreter's architecture:
`cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`

**`.version` is older than `rakupp --version`.** The library the loader
found is a leftover. A build directory keeps its `librakupp.*` files until a
build overwrites them, and a directory reconfigured without
`-DRAKUPP_BUILD_SHARED=ON` never does: the binary beside them stays current
while the library keeps the version it had. `make rakupp` rebuilds the binary
only; `cmake --build <dir>` with no target rebuilds the library too. Delete
the leftovers or rebuild the shared target; the loader cannot tell a leftover
from a fresh build. (The library reports the plain release number, the binary
adds its git describe suffix; the leading numbers should agree.)

**`AttributeError: dlsym(…, rk_…): symbol not found`.** Raised from
`interpreter()` when the library lacks an entry point this package declares,
which means the library predates the package. Rebuild it from the same
checkout the package came from.

**`import rakulang` is not the copy you edited.** `rakulang.__file__` says
which one loaded. `pip install -e bindings/python` imports the checkout
itself; a plain `pip install bindings/python`, or a wheel, copies the package
at install time and does not follow later edits, so reinstall to refresh.
`python` and `python3` can be different interpreters with different
site-packages.

**`rk_new refused: an interpreter is already live in this process`.**
Something already created an interpreter in this process. Use
`rakulang.interpreter()`, which returns the shared one.
