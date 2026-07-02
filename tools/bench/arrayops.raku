# A grep / map / sum pipeline over a range.
my $result = (1 .. 200_000).grep(* %% 3).map(* + 1).sum;
say $result;
