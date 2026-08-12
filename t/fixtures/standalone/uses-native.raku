use NativeCall;
sub sin(num64 --> num64) is native('m') { * }
say sin(0e0);
