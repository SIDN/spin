#!/bin/sh
# List the IP addresses on this machine, one per line, no netmask
force=
outfile=
while getopts "fho:" opt; do
  case $opt in
    h)
    echo "Usage: foobar"
    exit
    ;;
    f) force="yes"
    ;;
    o) outfile="$OPTARG"
    ;;
    \?) echo "Invalid option -$OPTARG" >&2
    ;;
  esac
done

RESULT=`ip addr show | grep inet | sed "s/^\s*//" | cut -f 2 -d ' ' | cut -f 1 -d '/'`

if [ -n "$outfile" ]; then
    if [ -e "$outfile" ]; then
        if [ -n "$force" ]; then
            echo "${RESULT}" > ${outfile};
        else
            echo "File exists already, use -f to overwrite";
        fi
    else
        echo "${RESULT}" > ${outfile};
    fi
else
    echo "${RESULT}"
fi
