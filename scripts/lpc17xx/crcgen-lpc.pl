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
#  3. Neither the name of the University nor the names of its contributors
#     may be used to endorse or promote products derived from this software
#     without specific prior written permission.
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

my @BOOTINFO_OFFSETS    = (0x100, 0x4100);
my $MAIN_PROGRAM_OFFSET = 0x4000;

sub crc_ccitt_update {
    my $crc  = shift;
    my $data = ord(shift);

    for (my $i = 0x80; $i > 0; $i >>= 1) {
        my $bit = $crc & (1 << 15);

        $bit = !$bit if ($data & $i);
        $crc <<= 1;
        $crc ^= 0x1021 if $bit;
    }

    return ($crc & 0xffff);
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

    substr($$bufref, $address + 6, 2) = $bincrc;
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
read IN, $buffer, (-s $filename) or die "Error reading file: $!";
close IN;

# pad up to specified length
if (length($buffer) < $length) {
    $buffer .= chr(0xff)x($length - length($buffer));
}

# update bootinfo blocks
foreach my $addr (@BOOTINFO_OFFSETS) {
    update_bootinfo(\$buffer, $addr, $devid, $version);
}

# calculate CRC over the updated data
my $crc = 0xffff;

for (my $i = $MAIN_PROGRAM_OFFSET; $i < $length; $i++) {
    $crc = crc_ccitt_update($crc, substr($buffer, $i, 1));
}

# insert CRCs
foreach my $addr (@BOOTINFO_OFFSETS) {
    update_crc(\$buffer, $addr, $crc);
}

# write new file
open OUT,">",$filename or die "Can't open $filename for writing: $!";
binmode OUT;
print OUT $buffer;
close OUT;
