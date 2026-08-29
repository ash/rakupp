// The Go guide's second example: Raku as a library, no grammar involved.
// Evaluate source, call subs with Go values, read results back. From a
// checkout (BUILD = an absolute path to a directory holding librakupp):
//
//	cd bindings/go
//	CGO_LDFLAGS="-L$BUILD -Wl,-rpath,$BUILD" go run ./examples/calc
package main

import (
	"fmt"
	"os"
	"strings"

	"rakulang"
)

func die(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func main() {
	// Eval runs source in the mainline scope and keeps it, like the REPL.
	v, err := rakulang.Eval("2 + 2")
	die(err)
	fmt.Println("2 + 2 =", v)

	// So loading a file of subs is just an Eval — they stay callable below.
	src, err := os.ReadFile("../examples/calc.raku")
	die(err)
	_, err = rakulang.Eval(string(src))
	die(err)

	// Call passes Go values as arguments and returns a Go value.
	area, err := rakulang.Call("area", 3, 4)
	die(err)
	fmt.Println("area(3, 4) =", area)

	// a Raku list -> a []interface{}
	primes, err := rakulang.Call("primes-below", 30)
	die(err)
	var out []string
	for _, p := range primes.([]interface{}) {
		out = append(out, fmt.Sprint(p))
	}
	fmt.Println("primes below 30:", strings.Join(out, " "))

	// a []interface{} -> a Raku list; a Raku hash -> a map[string]interface{}
	s, err := rakulang.Call("stats", []interface{}{3, 1, 4, 1, 5, 9, 2, 6})
	die(err)
	m := s.(map[string]interface{})
	fmt.Printf("stats: count=%v sum=%v mean=%v max=%v\n",
		m["count"], m["sum"], m["mean"], m["max"])

	greet, err := rakulang.Call("greet", map[string]interface{}{"name": "Ada", "age": 36})
	die(err)
	fmt.Println("greet:", greet)

	// Raku integers do not overflow; past 64 bits one crosses as a *big.Int.
	fact, err := rakulang.Call("factorial", 30)
	die(err)
	fmt.Println("30! =", fact)

	// A die inside Raku crosses as *RakuError, the host's own error type.
	if _, err := rakulang.Call("checked-div", 10, 0); err != nil {
		fmt.Println("died:", err)
	}
}
