#!/bin/sh
# List the IP addresses on this machine, one per line, no netmask
ip addr show | grep inet | sed "s/^\s*//" | cut -f 2 -d ' ' | cut -f 1 -d '/'
