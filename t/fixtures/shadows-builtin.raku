# Exercised by the interpreter and by every compile mode: a module sub whose
# name collides with a built-in. `--exe` used to print the BUILT-IN's answer for
# `val()` (Nil) because it resolved the call by name at compile time.
use BuiltinShadow;
say val();            # the export shadows the built-in `val`
say lc('AB');         # the built-in: the module's `lc` is not exported
say uses-its-own();   # …and inside the module, its own `lc` still wins
