unit module StandaloneDemo;
sub demo-triple($n) is export { $n * 3 }
sub demo-greet($who) is export { "hello, $who" }
