#!/usr/bin/perl
#
#  sd2iec - SD/MMC to Commodore serial bus interface/controller
#  Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>
#
#  Inspired by MMC2IEC by Lars Pontoppidan et al.
#
#  FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
#
#  configparser.pl: Overcomplicated generator for
#                   a Makefile include and autoconf.h
#

use File::Spec;
use Getopt::Long;
use Pod::Usage;
use warnings;
use strict;
use feature ':5.10';

# --- utility ---

# strips directories from the given file name
# also works for arrays
sub basename(\[$@]) {
    my $param = shift;
    local $_;

    if (ref $param eq "SCALAR") {
        return (File::Spec->splitpath($$param))[2];
    } else {
        return map { (File::Spec->splitpath($_))[2] } @$param;
    }
}

# unquotes a string, might do more in a later version
sub parse_string($) {
    my $str = shift;

    if ($str =~ /^\s*\"(.*)\"\s*$/) {
        return $1;
    } else {
        return $str;
    }
}

# returns a list of the config files passed on the command line
sub config_files() {
    my @list = ();

    foreach my $f (@ARGV) {
        push @list, split /,/, $f;
    }
    return @list;
}

# parse all config files, returns an array of the intended name and a hash of the entries
sub parse_config() {
    my $confname = "";
    my $prefix   = "";
    my %configitems;

    $configitems{CONFFILES} = "";
    foreach my $file (config_files()) {
        my $curname = basename($file);
        $curname =~ s/^(add)?config-?//;

        open FD,"<",$file or die "Can't open $file: $!";

        my $line;
        while (defined($line = <FD>)) {
            chomp $line;
            $line =~ s/^\s+//;
            $line =~ s/\s+$//;

            if ($line =~ /^#/) { # skip comments
                next;
            } elsif ($line =~ /^$/) { # skip empty lines
                next;
            } elsif ($line =~ /^NAME_OVERRIDE\s*=\s*(.*)$/) {
                $curname = parse_string($1);
            } elsif ($line =~ /^NAME_PREFIX\s*=\s*(.*)$/) {
                $prefix = parse_string($1);
            } elsif ($line =~ /^(CONFIG_[^ ]+)\s*=\s*([^#]+)/) {
                my $item  = $1;
                my $value = $2;
                $value =~ s/\s*$//;
                $configitems{$item} = $value;
            } else {
                say "Warning: Cannot parse line $. of file $file";
            }
        }
        close FD;
        $confname .= "-$curname" unless $curname eq "";
        $configitems{CONFFILES} .= " $file";
    }
    $confname = "-$prefix$confname" unless $prefix eq "";
    $configitems{CONFFILES} =~ s/^\s+//;

    return ($confname, %configitems);
}

# --- run modes ---

sub generate_files($$) {
    my $tgt_header  = shift;
    my $tgt_makeinc = shift;
    my %configitems;

    (undef, %configitems) = parse_config();

    # write files
    my @files = config_files();

    open HEADER,">",$tgt_header or die "Can't open $tgt_header: $!";
    open MAKE,">",$tgt_makeinc or die "Can't open $tgt_makeinc: $!";

    say HEADER "// ", basename($tgt_header), " generated from ",
               join(",",basename(@files)), " at ",scalar(localtime),"\n";
    say MAKE "# ", basename($tgt_makeinc), " generated from ",
               join(",",basename(@files)), " at ",scalar(localtime),"\n";

    my $configguard = uc(basename($tgt_header));
    $configguard =~ tr/A-Z0-9/_/cs;
    say HEADER "#ifndef $configguard";
    say HEADER "#define $configguard\n";

    foreach my $ci (sort keys %configitems) {
        next if lc($configitems{$ci}) eq "n";

        if (lc($configitems{$ci}) eq "y") {
            say HEADER "#define $ci";
            say MAKE   "$ci=y";
        } else {
            if ($configitems{$ci} =~ / /) {
                say HEADER "#define $ci \"$configitems{$ci}\"";
            } else {
                say HEADER "#define $ci $configitems{$ci}";
            }
            say MAKE "$ci=$configitems{$ci}";
        }
    }

    say HEADER "\n#endif";

    close HEADER;
    close MAKE;
}


sub generate_confdata() {
    my ($confname, %configitems);

    ($confname, %configitems) = parse_config();
    my $mcu = $configitems{CONFIG_MCU};
    $mcu =~ s/atmega/m/;
    say "$mcu$confname $configitems{CONFFILES}";
}

# --- main ---

my $run_mode    = "";
my $tgt_header  = "autoconf.h";
my $tgt_makeinc = "make.inc";

GetOptions(
    "confdata"   => sub { $run_mode ||= "confdata"; },
    "genfiles"   => sub { $run_mode ||= "genfiles"; },
    "header=s"   => \$tgt_header,
    "makeinc=s"  => \$tgt_makeinc,
    "help"       => sub { $run_mode   = "help";       },
    ) or pod2usage(2);

pod2usage(1) if $run_mode eq "help";

if ($run_mode eq "") {
    pod2usage(-message => "ERROR: No run mode specified",
              -verbose => 2, -exitval => 2, -noperldoc => 1);
} elsif ($run_mode eq "help") {
    pod2usage(-verbose => 2, -exitval => 0, -noperldoc => 1);
} elsif ($run_mode eq "confdata") {
    generate_confdata();
} elsif ($run_mode eq "genfiles") {
    generate_files($tgt_header, $tgt_makeinc);
}

=head1 SYNOPSIS

configparser [options] configfile [configfile...]

=head1 OPTIONS

=over 8

=item B<--help>

prints this help message

=item B<--confdata>

prints some things chosen front he config files to stdout

=item B<--genfiles>

parse the config files and output make/.h files

=item B<--header>

set the file name of the generated header file

=item B<--makeinc>

set the file name of the generated make include file

=back

=cut
