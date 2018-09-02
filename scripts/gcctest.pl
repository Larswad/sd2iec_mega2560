#!/usr/bin/env perl

use warnings;
use strict;
use feature ':5.10';

my $found_4_3 = 0;

while (<>) {
    next unless /gcc\s+\([^)]+\)\s+(\d+)\.(\d+)\./i;
    my $major = 0+$1;
    my $minor = 0+$2;
    if ($major > 4 || ($major == 4 && $minor > 2)) {
        $found_4_3 = 1;
    }
}

say "YES" if $found_4_3;
