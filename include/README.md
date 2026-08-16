# include/ — the headers that are NOT the compiler

Everything under `include/rakupp/` is public API: the surface an outside
compiler is allowed to see. Everything in `src/` is the interpreter's own
business, and no binding, embedder or extension should include it.

| file | who it is for | implemented by |
|---|---|---|
| `rakupp.h` | C embedders — `rk_ctx`, `rk_eval`, `rk_call`, the value handles | [`src/EmbedApi.cpp`](../src/EmbedApi.cpp) |
| `rakupp_ext.h` | native extension modules, the XS analogue ([EXTENSIONS.md](../docs/guide/EXTENSIONS.md)) | [`src/ExtApi.cpp`](../src/ExtApi.cpp) |
| `grammar.hpp` | the C++ binding — header-only sugar over `rakupp.h` | nothing; the engine never includes it |
| `rakupp_ext.dynlist` | the ELF link that puts `rk_*` in the executable's `.dynsym` | — |

The bindings in [`../bindings/`](../bindings) all sit on this: Python, JS, Go
and Rust `dlopen` librakupp and call the `rakupp.h` ABI through their own FFI;
C++ includes `<rakupp/grammar.hpp>` and links.

## Why the split is source-tree only

`cmake --install` collapses `include/rakupp/` and every header in `src/` into
one flat `<prefix>/include/rakupp/`. That is not sloppiness — `--exe` compiles
the C++ it generates against a single `-I` ([`main.cpp`](../src/main.cpp), the
`findRuntime` probe), and that generated code includes `Interpreter.h` and
`Value.h`. So the internal headers must ship, flat, beside the public ones.

Two consequences worth knowing before you move a file:

- **In-tree, both `include/` and `include/rakupp/` are on the include path**,
  mirroring what exists after an install. `#include "rakupp_ext.h"` from an
  internal header and `#include <rakupp/rakupp.h>` from an embedder are both
  correct, and both keep working installed.
- **Installed internals are not promises.** `Value.h` being reachable is a
  consequence of how `--exe` builds, not an invitation. The API is this
  directory.
