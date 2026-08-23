# A Sparrow6 scenario: Raku glue over tasks written in whatever language suits.
# Each task-run SPAWNS a process, which is why this framework is a fair test of
# an implementation's startup cost.
use Sparrow6::DSL;

task-run "tasks/hello";
task-run "tasks/greet", %( name => 'Raku++' );
task-run "tasks/report";
task-run "tasks/perl-check";
task-run "tasks/raku-ok";
