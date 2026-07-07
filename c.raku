my @nums = 3, 1, 4, 1, 5, 9, 2, 6;
await @nums.map: -> $n { start { sleep $n / 10; say $n } };
