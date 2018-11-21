#!/bin/sh

set -e

if [ $# -lt 1 ] || [ $# -gt 2 ] ; then
	echo "usage: $0 pcap-file [mqtt-host]" >&2
	exit 1
fi

FILE="${1}"
HOST="localhost"
[ -n "${2}" ] && HOST="${2}"

# -k 300
./pcap -r "${FILE}" | mosquitto_pub -l -h "${HOST}" -t SPIN/traffic

