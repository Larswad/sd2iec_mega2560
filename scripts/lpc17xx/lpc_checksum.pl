#!/usr/bin/perl -w

use strict;

if (scalar(@ARGV) != 1) {
    print "Usage: $0 binfile\n";
    exit 1;
}

open FD,"+<",$ARGV[0] or die "Can't open $ARGV[0]: $!";

my $buffer;
if (sysread(FD, $buffer, 4*7) != 4*7) {
    die "Short read from file: $!";
}

my @vecs = unpack("V*", $buffer);
my $checksum = 0;

while (scalar(@vecs) > 0) {
    $checksum += pop(@vecs);
}

$checksum = (-$checksum) & 0xffffffff;

if (syswrite(FD, pack("V", $checksum), 4) != 4) {
    die "Short write to file: $!";
}

close FD;
