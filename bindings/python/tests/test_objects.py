# Modules, objects and methods reached from Python: Interp.use, Interp.main,
# rakulang.Object. Run from the repository root:
#
#     RAKUPP_LIB=$PWD/build/librakupp.dylib python3 -m unittest discover -s bindings/python/tests
#
# The interpreter is one per process, so every test shares it; each test uses
# names of its own.

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import rakulang  # noqa: E402

raku = rakulang.interpreter()
raku.eval("use lib '%s'" % os.path.join(HERE, "lib").replace("\\", "/"))
geo = raku.use("Geo")


class ModuleTest(unittest.TestCase):

    def test_exported_sub(self):
        self.assertEqual(geo.area(3), 9)
        self.assertEqual(geo.area(3, 4), 12)       # a multi, either candidate

    def test_dashed_name(self):
        self.assertEqual(geo.far_away().x, 1000)
        self.assertEqual(getattr(geo, "far-away")().x, 1000)
        self.assertEqual(geo["far-away"]().x, 1000)

    def test_constant_and_enum(self):
        self.assertEqual(geo.PI_ISH, 3.14)
        self.assertEqual(str(geo.Green), "Green")
        self.assertEqual(int(geo.Green), 1)
        self.assertEqual(geo.Green.key, "Green")   # enum values have .key/.value
        self.assertEqual(geo.Green, geo.Green)

    def test_package_sub(self):
        self.assertEqual(geo.perimeter(3, 4), 14)  # `our sub`, not exported

    def test_tags(self):
        with self.assertRaises(AttributeError):
            geo.secret
        self.assertEqual(raku.use("Geo :extra").secret(), 42)

    def test_missing(self):
        with self.assertRaises(AttributeError):
            geo.no_such_thing
        self.assertFalse(hasattr(geo, "no_such_thing"))

    def test_dir(self):
        names = dir(geo)
        for n in ("Point", "origin", "area", "perimeter", "PI-ISH", "Color"):
            self.assertIn(n, names)

    def test_mainline_sees_the_import(self):
        self.assertEqual(raku.eval("area(5)"), 25)

    def test_module_that_is_a_class(self):
        st = raku.use("Stack").new()
        st.push(1).push(2)
        self.assertEqual(st.size(), 2)
        self.assertEqual(st.pop(), 2)

    def test_eval_sees_exported_values(self):
        self.assertEqual(raku.eval("origin().x"), 0)

    def test_bad_name(self):
        with self.assertRaises(ValueError):
            raku.use("; say 1")

    def test_failure_raises(self):
        with self.assertRaises(rakulang.RakuError) as cm:
            geo.broken()
        self.assertIn("no such shape", str(cm.exception))


class ObjectTest(unittest.TestCase):

    def test_new_with_named_arguments(self):
        p = geo.Point.new(x=3, y=4)
        self.assertIsInstance(p, rakulang.Object)
        self.assertEqual((p.x, p.y), (3, 4))
        self.assertEqual(str(p), "(3, 4)")
        self.assertIn("Geo::Point.new", repr(p))

    def test_methods(self):
        p = geo.Point.new(x=3, y=4)
        self.assertEqual(p.dist(geo.origin()), 5.0)
        self.assertEqual(p.moved(dx=1).x, 4)
        self.assertIsInstance(p.dist, rakulang.Method)

    def test_named_argument_spelling(self):
        # the signature says by_factor, so the underscore stays
        self.assertEqual(geo.Point.new(x=1, y=1).scaled(by_factor=3).x, 3)
        # the signature says from-x, so from_x is dashed
        self.assertEqual(geo.far_away(from_x=5).x, 1005)

    def test_rw_attribute(self):
        p = geo.Point.new(x=0, y=0)
        p.label = "home"
        self.assertEqual(p.label, "home")
        with self.assertRaises(rakulang.RakuError):
            p.x = 1                                # not `is rw`

    def test_objects_as_arguments(self):
        c = geo.centroid([geo.Point.new(x=0, y=0), geo.Point.new(x=2, y=4)])
        self.assertEqual((c.x, c.y), (1, 2))

    def test_containers_of_objects(self):
        cs = geo.corners()
        self.assertIsInstance(cs, list)
        self.assertEqual([str(c) for c in cs], ["(0, 0)", "(1, 1)"])
        d = geo.labels()
        self.assertEqual(d["n"], 3)
        self.assertEqual(d["a"].y, 2)

    def test_lazy_seq(self):
        ev = geo.evens()
        self.assertIsInstance(ev, rakulang.Object)
        got = []
        for x in ev:
            if len(got) == 5:
                break
            got.append(x)
        self.assertEqual(got, [0, 2, 4, 6, 8])
        self.assertEqual(ev[10], 20)

    def test_negative_index(self):
        xs = raku.main.EVAL("(1, 2, 3).Seq.lazy")
        self.assertEqual(xs[0], 1)
        ys = raku.main.EVAL("my class TBag { method AT-POS($i) { $i * 10 }; method elems { 3 } }; TBag.new")
        self.assertEqual(ys[-1], 20)               # *-1 on three elements is 2

    def test_pairs(self):
        p = raku.main.EVAL("a => 1")
        self.assertEqual((p.key, p.value), ("a", 1))

    def test_hashable(self):
        seen = {geo.Red: "r", geo.Blue: "b"}
        self.assertEqual(seen[geo.Blue], "b")

    def test_callable(self):
        f = geo.origin                              # a Sub, as an Object
        self.assertEqual(str(f()), "(0, 0)")

    def test_close(self):
        p = geo.Point.new(x=1, y=1)
        p.close()
        self.assertIn("released", repr(p))


class MainTest(unittest.TestCase):

    def test_classes_declared_by_eval(self):
        raku.eval("class TCounter { has $.n is rw = 0; "
                  "method bump(:$by = 1) { $!n += $by; self } }")
        c = raku.main.TCounter.new()
        c.bump().bump(by=5)
        self.assertEqual(c.n, 6)

    def test_core_routines_and_types(self):
        self.assertEqual(raku.main.sprintf("%03d", 7), "007")
        d = raku.main.Date.new(2026, 10, 5)
        self.assertEqual(d.year, 2026)
        self.assertEqual(str(d.later(days=3)), "2026-10-08")

    def test_call_with_named_arguments(self):
        raku.eval('sub tgreet($greeting, :$name, :$age = 0) { "$greeting, $name! ($age)" }')
        self.assertEqual(raku.call("tgreet", "Hi", name="Ada", age=36), "Hi, Ada! (36)")
        # `name` is a keyword argument too, not call()'s own parameter
        self.assertEqual(raku.call("tgreet", "Hi", name="Al"), "Hi, Al! (0)")

    def test_call_with_object_argument(self):
        raku.eval("sub tx($p) { $p.x }")
        self.assertEqual(raku.call("tx", geo.Point.new(x=7, y=0)), 7)

    def test_eval_is_unchanged(self):
        # eval still converts to plain Python data: an object as a string
        self.assertIsInstance(raku.eval("origin()"), str)


if __name__ == "__main__":
    unittest.main()
