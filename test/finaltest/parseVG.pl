#!/usr/bin/perl

use XML::Parser;
use XML::LibXML;

my $xmlfile="/tmp/sfcb-vgoutbad.xml";

open XML, '<', $xmlfile or die "Can't open input file";
my @contents = <XML>;
close XML;

@contents = grep(!/\/valgrindoutput/,@contents);
push(@contents,"</valgrindoutput>");

# Slurp and parse the valgrind xml output
#$dom = XML::LibXML->load_xml(@contents);
$dom = XML::LibXML->load_xml(
#      location => "/tmp/sfcb-vgout.xml"
      string => join(' ',@contents) 
    );
#Prep the output file
my $leakfile="./sfcbLeaks.xml";
open(LEAKFILE,">$leakfile");

# Spin through the xml output looking for definite leaks
my $defCount=0;
foreach my $ele ($dom->findnodes('/valgrindoutput/error')) {
    my($kind) = $ele->findnodes('./kind');
    if ($kind =~ m/Leak_DefinitelyLost/) {
        print LEAKFILE "$ele\n";
        $defCount++;
    }
  }

# If we had any, note it and return bad
if ($defCount) {
    close(LEAKFILE);
    print("  FAILED $defCount definite leaks. See $leakfile\n");
    exit($defCount);
}

# If no leaks, cleanup
unlink($leakfile);
close(LEAKFILE);
exit(0);
