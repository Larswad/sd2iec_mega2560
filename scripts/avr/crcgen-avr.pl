#!/usr/bin/env perl
#
# Generate bootloader CRC for the LPC176x version of sd2iec
#
#  Copyright (C) 2012-2017  Ingo Korb <ingo@akana.de>
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
#  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
#  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
#  SUCH DAMAGE.

use strict;
use warnings;
use feature ':5.10';

my $BOOTINFO_SIZE = 8;
my $CRC_SIZE      = 2;

# translated from avr-libc version because it appears that
# it implements a non-standard variant of this CRC
sub crc_ccitt_update {
    my $crc  = shift;
    my $data = ord(shift);

    $data ^= $crc & 0xff;
    $data = ($data ^ ($data << 4)) & 0xff;

    return ((($data << 8) & 0xffff) | (($crc >> 8) & 0xff))     ^
           ((($data >> 4) & 0xff)   ^ (($data << 3) & 0xffff));
}

# insert new data in a bootinfo block
sub update_bootinfo {
    my $bufref  = shift;
    my $address = shift;
    my $devid   = shift;
    my $version = shift;

    my $bootinfo = pack("Vvv", $devid, $version, 0);

    substr($$bufref, $address, length($bootinfo)) = $bootinfo;
}

# insert new CRC in a bootinfo block
sub update_crc {
    my $bufref  = shift;
    my $address = shift;
    my $crc     = shift;

    my $bincrc  = pack("v", $crc);

    substr($$bufref, $address, 2) = $bincrc;
}

# parse command line
if (scalar(@ARGV) != 4) {
    say "Usage: $0 <filename> <length> <signature> <version>";
    exit 1;
}

my $filename = shift;
my $length   = shift;
my $devid    = hex shift;
my $version  = hex shift;

if ($length =~ /^0/) {
    $length = oct $length;
} else {
    $length = 0+$length;
}

# samity checks
if (! -e $filename) {
    say STDERR "ERROR: Input file >$filename< does not exist";
    exit 2;
}

if (-s $filename > $length) {
    say STDERR "ERROR: Input file is longer than specified length";
    exit 2;
}

# read file into buffer
my $buffer;

open IN,"<",$filename or die "Can't open $filename: $!";
binmode IN;
if (-s $filename > 0) {
    read IN, $buffer, (-s $filename) or die "Error reading file: $!";
} else {
    # accept empty files for testing
    $buffer = "";
}
close IN;

# pad up to specified length
if (length($buffer) < $length) {
    $buffer .= chr(0xff)x($length - length($buffer));
}

# update bootinfo block
update_bootinfo(\$buffer, $length - $BOOTINFO_SIZE, $devid, $version);

# calculate CRC
my $crc = 0xffff;

for (my $i = 0; $i < $length - $CRC_SIZE; $i++) {
    $crc = crc_ccitt_update($crc, substr($buffer, $i, 1));
}

# insert CRC
update_crc(\$buffer, $length - $CRC_SIZE, $crc);

# write new file
open OUT,">",$filename or die "Can't open $filename for writing: $!";
binmode OUT;
print OUT $buffer;
close OUT;
