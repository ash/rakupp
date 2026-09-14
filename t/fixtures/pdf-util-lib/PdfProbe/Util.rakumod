# A module in the shape PDF::COS::Util has: several proto subs, each exported
# under a tag of its own name, with the candidates declared beside them.
unit module PdfProbe::Util;

proto sub to-ast(|)      is export(:to-ast)      {*};
multi sub to-ast($x)     { "to:" ~ $x }

proto sub ast-coerce(|)  is export(:ast-coerce)  {*};
multi sub ast-coerce($x) { "coerce:" ~ $x }

proto sub from-ast(|)    is export(:from-ast)    {*};
multi sub from-ast($x)   { "from:" ~ $x }
