\ rakupp-forth demo — words grow the language from the stack up.

." 3 4 + 2 * = "  3 4 + 2 * .  cr

\ define new words in terms of old ones
: square ( n -- n*n )  dup * ;
: cube   ( n -- n^3 )  dup square * ;
." 5 square = "  5 square .  cr
." 3 cube   = "  3 cube .  cr

\ conditionals: if / else / then
: sign ( n -- )
   dup 0> if ." positive" drop
   else       0< if ." negative" else ." zero" then
   then ;
." sign 7:  "   7 sign  cr
." sign -3: "  -3 sign  cr
." sign 0:  "   0 sign  cr

\ counted loop with the loop index i
: fact ( n -- n! )  1 swap 1+ 1 do i * loop ;
." 5!  = "   5 fact .  cr
." 10! = "  10 fact .  cr

\ begin / until
: countdown ( n -- )  begin dup . 1- dup 0= until drop ;
." countdown from 5: "  5 countdown  cr

\ do loop building a Fibonacci number
: fib ( n -- fib )  0 1 rot 0 do over + swap loop drop ;
." fib 10 = "  10 fib .  cr

\ a stars-drawing word
: stars ( n -- )  0 do 42 emit loop ;
: box ( w h -- )  0 do dup stars cr loop drop ;
." a 10x3 box:"  cr  10 3 box
