#!/usr/bin/perl
use strict;
use warnings;
local $/;
open my $old_file, '<', $ARGV[0] or die $!;
open my $new_file, '<', $ARGV[1] or die $!;
my $old = <$old_file>;
my $new = <$new_file>;
my %section = (
    MODEL_CODE => '.stage_runtime_code',
    PCI_CODE => '.stage_pci_code',
    XHCI_CODE => '.stage_xhci_code',
);
my $uses = 0;
for my $name (sort keys %section) {
    my $replacement = '__section("' . $section{$name} . '") __noinline';
    my $definitions = $old =~ s/^#define \Q$name\E \Q$replacement\E\n//mg;
    die "expected one original decorator definition\n" unless $definitions == 1;
    $uses += $old =~ s/\b\Q$name\E\b/$replacement/g;
}
die "expected fourteen decorator uses\n" unless $uses == 14;
sub tokens {
    my ($text) = @_;
    my @tokens;
    while ($text =~ m{
        /\*.*?\*/ | //[^\n]* | \s+ |
        ("(?:\\.|[^"\\])*" | '(?:\\.|[^'\\])*' |
         [A-Za-z_][A-Za-z0-9_]* | [0-9]+ | .)
    }gsx) {
        push @tokens, $1 if defined $1;
    }
    return join("\0", @tokens);
}
die "expanded C token streams differ\n" unless tokens($old) eq tokens($new);
print "Three exact decorator expansions, fourteen uses, complete C tokens identical\n";
