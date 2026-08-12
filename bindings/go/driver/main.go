// The Go half of the byte-identical gate: the same walk as driver.py,
// through the rakulang package (cgo over librakupp). Run by
// grammar-smoke.raku; outputs are compared byte for byte.
//
//	CGO_LDFLAGS="-L$BUILD -Wl,-rpath,$BUILD" go run ./driver <grammar> <input>
package main

import (
	"errors"
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"

	"rakulang"
)

func canon(x interface{}) string {
	switch v := x.(type) {
	case []interface{}:
		parts := make([]string, len(v))
		for i, e := range v {
			parts[i] = canon(e)
		}
		return "[" + strings.Join(parts, ",") + "]"
	case map[string]interface{}:
		keys := make([]string, 0, len(v))
		for k := range v {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		parts := make([]string, len(keys))
		for i, k := range keys {
			parts[i] = k + ":" + canon(v[k])
		}
		return "{" + strings.Join(parts, ",") + "}"
	case nil:
		return ""
	case int64:
		return strconv.FormatInt(v, 10)
	case bool:
		if v {
			return "True"
		}
		return "False"
	default:
		return fmt.Sprint(v)
	}
}

func rakuBool(b bool) string {
	if b {
		return "True"
	}
	return "False"
}

func main() {
	grammarFile, inputFile := os.Args[1], os.Args[2]

	g, err := rakulang.FromFile(grammarFile, "Log", "LogActions")
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	input, _ := os.ReadFile(inputFile)
	m, err := g.Parse(string(input))
	if err != nil {
		fmt.Fprintln(os.Stderr, "gate: the log corpus did not parse:", err)
		os.Exit(1)
	}
	defer m.Close()

	var out strings.Builder
	lines := m.Get("line")
	n := lines.Len()
	fmt.Fprintf(&out, "lines %d\n", n)
	fmt.Fprintf(&out, "islist %s\n", rakuBool(lines.IsList()))

	for i := 0; i < n; i++ {
		fmt.Fprintf(&out, "%s %s\n", lines.At(i).Get("ip").Str(), lines.At(i).Get("status").Str())
	}

	fmt.Fprintf(&out, "made %s\n", canon(m.Get("line").At(0).Get("size").Made()))
	fmt.Fprintf(&out, "req.str %s\n", m.Get("line").At(42).Get("req").Str())
	fmt.Fprintf(&out, "size.int %d\n", m.Get("line").At(42).Get("size").Int())
	fmt.Fprintf(&out, "missing %s\n", rakuBool(m.Get("nope").Truthy()))
	fmt.Fprintf(&out, "tree %s\n", canon(m.Get("line").At(999).Tree()))

	one, err := g.Parse("7.7.7.7 - - [x] \"GET / HTTP/1.1\" 200 5\n", "line")
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Fprintf(&out, "rule-parse %s\n", one.Get("status").Str())
	one.Close()

	_, err = g.Parse("this is not a log line")
	if errors.Is(err, rakulang.ErrNoMatch) {
		fmt.Fprintf(&out, "failed-parse None\n")
	} else {
		fmt.Fprintf(&out, "failed-parse Match\n")
	}

	_, err = g.ParseStrict("this is not a log line")
	var pe *rakulang.ParseError
	if errors.As(err, &pe) {
		fmt.Fprintf(&out, "diag line %d col %d rule %s\n", pe.Line, pe.Col, pe.Rule)
	} else {
		fmt.Fprintf(&out, "diag none\n")
	}

	fmt.Print(out.String())
}
