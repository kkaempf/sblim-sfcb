#!/usr/bin/perl

use XML::Parser;
use XML::LibXML;

my $vgfile="/tmp/sfcb-vgout";
my $leakfile="./sfcbLeaks.txt";
my %ht;
my $defCount=0;

open VGO, '<', $vgfile or die "Can't open input file";
my @contents = <VGO>;
close VGO;

#Split the output by PID, which is the first token on 
#each line
foreach my $line (@contents)
{
    my ($pid,$txt)=split(/ /,$line,2);
    push(@{$ht{$pid}},$txt);
}

#Prep the output file
open(LEAKFILE,">$leakfile");

#Look through the split up output for definite leaks
foreach my $pid(keys %ht)
{
    my $leakfound=0;
    foreach my $line (@{$ht{$pid}})
    {
        if ($leakfound && $line =~ /^$/){
            #Blank line ends the stanza
            print LEAKFILE $line;
            $leakfound=0;
        } elsif ($leakfound) {
            #Still in the stanza, keep printing
            print LEAKFILE $line;
        } elsif ($line =~ m/.*are definitely lost.*/){
            #Found a leak, print it and its stanza
            #to the output file
            print LEAKFILE $line;
            $leakfound=1;
            $defCount++;
        }
    }
}
close(LEAKFILE);

# If we had any, note it and return the count, or 0 if none.
if ($defCount) {
    print("  FAILED $defCount definite leaks. See $leakfile\n");
    exit($defCount);
} else {
    print("  PASSED no definite leaks.\n");
    unlink($leakfile);
    exit(0);
}
