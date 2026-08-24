unit module LibA;
use NativeCall;
constant LIB = 'no-such-library-a';
sub a-pid(--> int32) is native(LIB) is symbol('getpid') is export(:x) { * }
