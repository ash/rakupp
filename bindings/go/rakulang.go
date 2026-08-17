// Package rakulang — Raku grammars for Go, over librakupp (cgo).
//
// The Go face of the grammar service (docs/dev/plans/GRAMMAR-PLAN.md): the
// grammar stays a .raku file, parsing happens in an embedded Raku++
// interpreter, and the Raku shim it drives ships INSIDE the library
// (rk_grammar_shim) — this package is invocation, results and lifetime.
//
//	g, err := rakulang.FromFile("log.raku", "Log", "LogActions")
//	m, err := g.Parse(text)          // err == ErrNoMatch when it does not match
//	defer m.Close()                  // a rooted native value: EXPLICIT free —
//	                                 // no SetFinalizer, by design
//	lines := m.Get("line")
//	for i := 0; i < lines.Len(); i++ {
//	    fmt.Println(lines.At(i).Get("ip").Str(), lines.At(i).Get("status").Str())
//	}
//
// Build with the library visible to cgo and the loader:
//
//	CGO_LDFLAGS="-L/path/to/lib -Wl,-rpath,/path/to/lib" go build
//
// One interpreter per process, created on first use. One goroutine talks to
// the interpreter at a time — a Grammar/Match is NOT safe to share across
// goroutines (Raku code inside the interpreter threads as it pleases; the
// boundary is single-file). This is the thread contract the plan says to put
// in the type, not the docs: nothing here is Send-able, and there is no
// internal locking to pretend otherwise.
package rakulang

/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#cgo LDFLAGS: -lrakupp
#include <stdlib.h>
#include <rakupp/rakupp.h>
*/
import "C"

import (
	"errors"
	"fmt"
	"os"
	"sort"
	"sync"
	"unsafe"
)

// ErrNoMatch is returned by Parse when the grammar does not match the input.
// ParseStrict returns a *ParseError carrying the engine's diagnosis instead.
var ErrNoMatch = errors.New("rakulang: no match")

// RakuError is a Raku-side failure (a die, broken grammar source, …).
type RakuError struct{ Message string }

func (e *RakuError) Error() string { return e.Message }

// ParseError is a diagnosed non-match: where the parse reached and which rule
// was trying there (1-based Line/Col; Pos is a 0-based character offset).
type ParseError struct {
	Label string
	Pos   int
	Line  int
	Col   int
	Rule  string
}

func (e *ParseError) Error() string {
	return fmt.Sprintf("%s: no match — failed at line %d column %d while trying <%s>",
		e.Label, e.Line, e.Col, e.Rule)
}

type session struct {
	rk C.RkInterp
	c  C.RkCtx
}

var (
	ses     *session
	sesErr  error
	sesOnce sync.Once
)

func get() (*session, error) {
	sesOnce.Do(func() {
		rk := C.rk_new(nil)
		if rk == nil {
			sesErr = &RakuError{"rk_new refused: an interpreter is already live in this process"}
			return
		}
		s := &session{rk: rk, c: C.rk_ctx(rk)}
		if C.rk_eval(rk, C.rk_grammar_shim(), nil) != C.RK_OK {
			sesErr = &RakuError{"grammar shim failed to load: " + C.GoString(C.rk_last_error(rk))}
			return
		}
		ses = s
	})
	return ses, sesErr
}

func (s *session) str(v string) C.RkValue {
	cs := C.CString(v)
	defer C.free(unsafe.Pointer(cs))
	return C.rk_str(s.c, cs, C.size_t(len(v)))
}

func (s *session) call(name string, args []C.RkValue) (C.RkValue, error) {
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	var argv *C.RkValue
	if len(args) > 0 {
		argv = &args[0]
	}
	r := C.rk_call(s.c, cn, argv, C.size_t(len(args)))
	if r == nil {
		msg := name + " failed"
		if e := C.rk_error(s.c); e != nil {
			msg = C.GoString(e)
		}
		C.rk_clear_error(s.c)
		return nil, &RakuError{msg}
	}
	return r, nil
}

func (s *session) goStr(v C.RkValue) string {
	var n C.size_t
	p := C.rk_str_get(s.c, v, &n)
	if p == nil || n == 0 {
		return ""
	}
	return C.GoStringN(p, C.int(n))
}

// toGo converts an engine value to nil / bool / int64 / float64 / string /
// []interface{} / map[string]interface{} — the Tree() shape.
func (s *session) toGo(v C.RkValue) interface{} {
	switch C.rk_type(s.c, v) {
	case C.RK_ANY:
		return nil
	case C.RK_BOOL:
		return C.rk_truthy(s.c, v) != 0
	case C.RK_INT:
		return int64(C.rk_int_get(s.c, v))
	case C.RK_NUM, C.RK_RAT:
		return float64(C.rk_num_get(s.c, v))
	case C.RK_ARRAY:
		n := int(C.rk_elems(s.c, v))
		out := make([]interface{}, n)
		for i := 0; i < n; i++ {
			out[i] = s.toGo(C.rk_at_pos(s.c, v, C.size_t(i)))
		}
		return out
	case C.RK_HASH:
		n := int(C.rk_elems(s.c, v))
		out := make(map[string]interface{}, n)
		for i := 0; i < n; i++ {
			var kl C.size_t
			kp := C.rk_key_at(s.c, v, C.size_t(i), &kl)
			out[C.GoStringN(kp, C.int(kl))] = s.toGo(C.rk_val_at(s.c, v, C.size_t(i)))
		}
		return out
	default: // RK_STR / RK_OTHER stringify
		return s.goStr(v)
	}
}

// fromGo converts a Go value to an engine value: nil, bool, the integer and
// float kinds, string, []interface{} and map[string]interface{}. The result
// is UNROOTED — valid until the next Eval or Call, which is long enough to
// pass it as an argument and no longer.
func (s *session) fromGo(x interface{}) (C.RkValue, error) {
	switch v := x.(type) {
	case nil:
		return C.rk_any(s.c), nil
	case bool:
		b := C.int(0)
		if v {
			b = 1
		}
		return C.rk_bool(s.c, b), nil
	case int:
		return C.rk_int(s.c, C.longlong(v)), nil
	case int32:
		return C.rk_int(s.c, C.longlong(v)), nil
	case int64:
		return C.rk_int(s.c, C.longlong(v)), nil
	case float32:
		return C.rk_num(s.c, C.double(v)), nil
	case float64:
		return C.rk_num(s.c, C.double(v)), nil
	case string:
		return s.str(v), nil
	case []interface{}:
		a := C.rk_array(s.c)
		for _, item := range v {
			e, err := s.fromGo(item)
			if err != nil {
				return nil, err
			}
			C.rk_push(s.c, a, e)
		}
		return a, nil
	case map[string]interface{}:
		h := C.rk_hash(s.c)
		for _, k := range SortedKeys(v) {
			e, err := s.fromGo(v[k])
			if err != nil {
				return nil, err
			}
			ck := C.CString(k)
			C.rk_set(s.c, h, ck, C.size_t(len(k)), e)
			C.free(unsafe.Pointer(ck))
		}
		return h, nil
	}
	return nil, &RakuError{fmt.Sprintf("cannot pass a %T to Raku", x)}
}

// Eval evaluates Raku source in the interpreter's mainline scope and returns
// the last statement's value as a Go value. State persists across calls,
// exactly as in the REPL: Eval a `sub` here and Call finds it afterwards.
func Eval(source string) (interface{}, error) {
	s, err := get()
	if err != nil {
		return nil, err
	}
	cs := C.CString(source)
	defer C.free(unsafe.Pointer(cs))
	var out C.RkValue
	if C.rk_eval(s.rk, cs, &out) != C.RK_OK {
		msg := "rk_eval failed"
		if e := C.rk_last_error(s.rk); e != nil {
			msg = C.GoString(e)
		}
		return nil, &RakuError{msg}
	}
	return s.toGo(out), nil
}

// Call invokes a Raku routine by name with Go arguments and returns a Go
// value: rakulang.Call("area", 3, 4). The routine must be visible in the
// mainline scope — declared by an earlier Eval, or by a file you evaluated.
// A die inside it comes back as *RakuError.
func Call(name string, args ...interface{}) (interface{}, error) {
	s, err := get()
	if err != nil {
		return nil, err
	}
	argv := make([]C.RkValue, len(args))
	for i, a := range args {
		if argv[i], err = s.fromGo(a); err != nil {
			return nil, err
		}
	}
	r, err := s.call(name, argv)
	if err != nil {
		return nil, err
	}
	return s.toGo(r), nil
}

// Can reports whether the mainline scope has a routine of this name.
func Can(name string) bool {
	s, err := get()
	if err != nil {
		return false
	}
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	return C.rk_can(s.c, cn) != 0
}

// Version is the engine's version string, e.g. "3.14.0".
func Version() string { return C.GoString(C.rk_version()) }

// Grammar is a compiled Raku grammar. Identical source compiles once; named
// compiles are isolated per compile, so recompiling an edited grammar never
// rebinds an earlier Grammar's body.
type Grammar struct {
	id    int64
	label string
}

// FromSource compiles grammar source. name is the grammar's name in the
// source (empty only when the grammar declaration is the source's last
// statement); actions names an actions class in the same source.
func FromSource(source, name, actions string) (*Grammar, error) {
	s, err := get()
	if err != nil {
		return nil, err
	}
	id, err := s.call("rk-grammar-compile", []C.RkValue{s.str(source), s.str(name), s.str(actions)})
	if err != nil {
		return nil, err
	}
	label := name
	if label == "" {
		label = "<anonymous>"
	}
	return &Grammar{id: int64(C.rk_int_get(s.c, id)), label: label}, nil
}

// FromFile reads and compiles a grammar file — the documented default.
func FromFile(path, name, actions string) (*Grammar, error) {
	src, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	g, err := FromSource(string(src), name, actions)
	if err != nil {
		return nil, err
	}
	g.label = path
	return g, nil
}

// Parse matches text against the grammar (rule "" = the grammar's default).
// Returns ErrNoMatch when it does not match; the whole input must match.
func (g *Grammar) Parse(text string, rule ...string) (*Match, error) {
	return g.parse(text, first(rule), false)
}

// ParseStrict is Parse with a diagnosed failure: a *ParseError carrying the
// engine's highwater (line, column, rule) instead of ErrNoMatch.
func (g *Grammar) ParseStrict(text string, rule ...string) (*Match, error) {
	return g.parse(text, first(rule), true)
}

func first(v []string) string {
	if len(v) > 0 {
		return v[0]
	}
	return ""
}

func (g *Grammar) parse(text, rule string, strict bool) (*Match, error) {
	s, err := get()
	if err != nil {
		return nil, err
	}
	raw, err := s.call("rk-grammar-parse", []C.RkValue{C.rk_int(s.c, C.longlong(g.id)), s.str(text), s.str(rule)})
	if err != nil {
		return nil, err
	}
	if C.rk_type(s.c, raw) == C.RK_ANY {
		if strict {
			d, derr := s.call("rk-grammar-diagnosis", []C.RkValue{s.str(text)})
			if derr == nil && C.rk_type(s.c, d) == C.RK_HASH {
				t := s.toGo(d).(map[string]interface{})
				return nil, &ParseError{
					Label: g.label,
					Pos:   int(t["pos"].(int64)),
					Line:  int(t["line"].(int64)),
					Col:   int(t["col"].(int64)),
					Rule:  t["rule"].(string),
				}
			}
		}
		return nil, ErrNoMatch
	}
	m := &Match{}
	m.holder = &rooted{h: C.rk_root(s.c, raw)}
	m.Node = Node{holder: m.holder}
	return m, nil
}

type rooted struct{ h C.RkValue }

// Match owns a rooted engine value. Close it — deterministically, per Go
// idiom; there is deliberately no finalizer.
type Match struct {
	Node
	holder *rooted
}

// Close releases the rooted value. Every Node derived from this Match is
// dead afterwards.
func (m *Match) Close() {
	if m.holder != nil && m.holder.h != nil {
		C.rk_unroot(ses.c, m.holder.h)
		m.holder.h = nil
	}
}

// Node is a lazy path under a Match: Get/At accumulate, terminals cross the
// boundary once (Str, Int, Len, Tree, …).
type Node struct {
	holder *rooted
	steps  []interface{} // string (named capture) | int (positional / index)
}

func (n Node) Get(key string) Node {
	return Node{holder: n.holder, steps: append(append([]interface{}{}, n.steps...), key)}
}

func (n Node) At(i int) Node {
	return Node{holder: n.holder, steps: append(append([]interface{}{}, n.steps...), i)}
}

func (n Node) walk(op string) (C.RkValue, error) {
	s, err := get()
	if err != nil {
		return nil, err
	}
	path := C.rk_array(s.c)
	for _, st := range n.steps {
		switch v := st.(type) {
		case int:
			C.rk_push(s.c, path, C.rk_int(s.c, C.longlong(v)))
		case string:
			C.rk_push(s.c, path, s.str(v))
		}
	}
	return s.call("rk-match-walk", []C.RkValue{n.holder.h, path, s.str(op)})
}

func (n Node) mustWalk(op string) C.RkValue {
	v, err := n.walk(op)
	if err != nil {
		panic(err) // a missing capture asked for as str/int — programmer error, per Go's Must* idiom
	}
	return v
}

// Str returns the matched text at this node (panics if nothing matched here —
// probe with Truthy first, exactly like the bool()/len() rule in the twins).
func (n Node) Str() string { return ses.goStr(n.mustWalk("str")) }

// Int returns the node's text as an integer.
func (n Node) Int() int64 { return int64(C.rk_int_get(ses.c, n.mustWalk("int"))) }

// Num returns the node's text as a float.
func (n Node) Num() float64 { return float64(C.rk_num_get(ses.c, n.mustWalk("num"))) }

// Truthy answers whether anything matched at this path.
func (n Node) Truthy() bool { return C.rk_truthy(ses.c, n.mustWalk("bool")) != 0 }

// IsList answers whether this is a quantified capture (a list of matches).
func (n Node) IsList() bool { return C.rk_truthy(ses.c, n.mustWalk("islist")) != 0 }

// Len is the list length (1 for a bare node, 0 for a missing one).
func (n Node) Len() int { return int(C.rk_int_get(ses.c, n.mustWalk("elems"))) }

// Tree is the eager conversion of everything below this node (~1.4x the
// parse; prefer the lazy path when you want less than about half of it).
func (n Node) Tree() interface{} { return ses.toGo(n.mustWalk("tree")) }

// Made is what the actions class computed here, or nil.
func (n Node) Made() interface{} { return ses.toGo(n.mustWalk("made")) }

// SortedKeys is a small helper for deterministic iteration over Tree() maps.
func SortedKeys(m map[string]interface{}) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}
