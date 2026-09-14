# Loaded first, and asks for only two of the three names — as PDF::COS::Dict
# does before PDF::IO::Serializer asks for the third.
unit class PdfProbe::First;
use PdfProbe::Util :&from-ast, :&ast-coerce;
method go { from-ast(1) ~ ' ' ~ ast-coerce(2) }
