use GUI::Wings;

app 'Calculator', {
    # $val is the EXACT value of what the display shows: the display may round
    # 1/3 to 0,333333, but the math keeps the Rat — 1 ÷ 3 × 3 is exactly 1.
    my ($acc, $val, $op, $fresh) = 0, 0, '', True;

    window :title('Calculator'), :size(296, 388), :fixed, {
        my $display = label '0', :font(40), :mono, :align<right>,
                                 :at(16, 316), :size(264, 56);

        # the display speaks decimal comma; the math speaks Raku
        my sub to-num(Str $s) {
            my $t = $s.subst(',', '.');
            $t = $t.chop if $t.ends-with('.');
            +($t || '0');
        }
        my sub fmt($v) {
            my $s = $v.Str;
            $s = sprintf('%.7g', $v.Num) if $s.chars > 11;
            $s.subst('.', ',');
        }
        my sub press(Str $k) {
            my $shown = $display.text;
            if $k eq 'C' {
                ($acc, $val, $op, $fresh) = 0, 0, '', True;
                $display.text = '0';
            }
            elsif $k ~~ /^\d$/ {
                $display.text = $fresh || $shown eq '0' ?? $k !! $shown ~ $k;
                $val = to-num($display.text);
                $fresh = False;
            }
            elsif $k eq ',' {
                if $fresh {
                    $display.text = '0,';
                    $fresh = False;
                }
                elsif !$shown.contains(',') {
                    $display.text = $shown ~ ',';
                }
                $val = to-num($display.text);
            }
            elsif $shown ne 'Err' {
                if $op && !$fresh {
                    $acc = do given $op {
                        when '+' { $acc + $val }
                        when '−' { $acc - $val }
                        when '×' { $acc * $val }
                        when '÷' { $val == 0 ?? Nil !! $acc / $val }
                    }
                }
                elsif !$op {
                    $acc = $val;
                }
                if $acc.defined {
                    $display.text = fmt($acc);
                    $val = $acc;            # carry the exact result forward
                }
                else {
                    $display.text = 'Err';
                    ($acc, $val) = 0, 0;
                }
                $op = $k eq '=' ?? '' !! $k;
                $fresh = True;
            }
        }

        my @keys = <7 8 9 ÷  4 5 6 ×  1 2 3 −  C 0 , +>;
        my @buttons = @keys.kv.map: -> $i, $k {
            button $k, :font(24), :size(60, 52),
                   :at(16 + ($i mod 4) * 68, 72 + (3 - $i div 4) * 60),
                   tint => $k eq 'C'        ?? 'gray'
                        !! $k ~~ /<[÷×−+]>/ ?? 'orange'
                        !! '';
        }
        @buttons.push: button '=', :font(24), :size(264, 44), :at(16, 16), :tint<orange>;

        react {
            for @buttons -> $b {
                whenever $b.clicks { press($b.title) }
            }
            whenever signal(SIGINT) { done }
        }
    }
}
