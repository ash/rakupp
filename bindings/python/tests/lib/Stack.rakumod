# A module that is itself a class: Interp.use hands back the class's methods.
unit class Stack;

has @!items;

method push($x) { @!items.push($x); self }
method pop()    { @!items.pop }
method size()   { @!items.elems }
