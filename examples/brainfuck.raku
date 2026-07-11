#!/usr/bin/env raku
# A tiny Brainfuck interpreter. Brainfuck has just eight instructions that
# operate on a tape of byte cells and a single data pointer:
#
#   >  move the pointer right      <  move the pointer left
#   +  increment the cell          -  decrement the cell
#   .  output the cell as a char   ,  read a char (unused here)
#   [  jump past the matching ]    ]  jump back to the matching [
#      if the current cell is 0       if the current cell is non-zero
#
# The embedded program below is the classic "Hello World!" — the loops set
# up the ASCII codes on the tape, and the `.` commands print them.
#
# Shows off array/pointer manipulation, character/string building, and a
# small dispatch loop with matched-bracket control flow.

my $program =
    '++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]' ~
    '>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.';

sub run($code) {
    my @prog = $code.comb;
    my @tape = 0 xx 30000;
    my $ptr  = 0;
    my $pc   = 0;
    my $out  = '';

    while $pc < @prog.elems {
        my $cmd = @prog[$pc];
        if $cmd eq '>' {
            $ptr++;
        }
        elsif $cmd eq '<' {
            $ptr--;
        }
        elsif $cmd eq '+' {
            @tape[$ptr] = (@tape[$ptr] + 1) % 256;
        }
        elsif $cmd eq '-' {
            @tape[$ptr] = (@tape[$ptr] + 255) % 256;
        }
        elsif $cmd eq '.' {
            $out ~= chr(@tape[$ptr]);
        }
        elsif $cmd eq '[' {
            if @tape[$ptr] == 0 {
                # Skip forward to the matching ']'.
                my $depth = 1;
                while $depth > 0 {
                    $pc++;
                    $depth++ if @prog[$pc] eq '[';
                    $depth-- if @prog[$pc] eq ']';
                }
            }
        }
        elsif $cmd eq ']' {
            if @tape[$ptr] != 0 {
                # Jump back to the matching '['.
                my $depth = 1;
                while $depth > 0 {
                    $pc--;
                    $depth++ if @prog[$pc] eq ']';
                    $depth-- if @prog[$pc] eq '[';
                }
            }
        }
        $pc++;
    }
    $out;
}

print run($program);
