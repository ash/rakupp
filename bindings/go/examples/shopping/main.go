// The Go guide's example: compile a grammar from a .raku file, parse text,
// and move the results into Go. From a checkout (BUILD = an absolute path
// to a directory holding librakupp):
//
//	cd bindings/go
//	CGO_LDFLAGS="-L$BUILD -Wl,-rpath,$BUILD" go run ./examples/shopping
package main

import (
	"errors"
	"fmt"
	"os"

	"rakulang"
)

func main() {
	g, err := rakulang.FromFile("../examples/shopping.raku", "Shopping", "ShoppingActions")
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	m, err := g.Parse("milk=2\nbread=1\neggs=12\n") // ErrNoMatch if no match
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer m.Close() // a Match holds a rooted engine value — free it explicitly

	items := m.Get("item")
	fmt.Println(items.Len(), "items")
	for i := 0; i < items.Len(); i++ { // lazy: one engine call per leaf
		item := items.At(i)
		fmt.Println(item.Get("name").Str(), "x", item.Get("qty").Int())
	}

	fmt.Println("total, computed in Raku:", m.Made()) // ShoppingActions made this

	fmt.Println("as plain Go data:", m.Tree())

	// Parse returns ErrNoMatch on a non-match; ParseStrict diagnoses it.
	_, err = g.ParseStrict("milk=2\nbread=lots\n")
	var pe *rakulang.ParseError
	if errors.As(err, &pe) {
		fmt.Printf("line %d column %d while trying <%s>\n", pe.Line, pe.Col, pe.Rule)
	}
}
