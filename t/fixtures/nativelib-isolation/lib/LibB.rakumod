unit module LibB;
use NativeCall;
constant LIB = Str;
sub b-pid(--> int32) is native(LIB) is symbol('getpid') is export { * }
