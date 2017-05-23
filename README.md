
This is an early prototype of the SPIN platform.

As of right now, it consists of two very basic modules:
- a collector that reads nflog output, aggregates it, and sends it to a local mosquitto server
- a few html pages to process the traffic data


Prerequisites:
Software:
- luaposix
- luabitop
- luasocket
- libnetfilter-log
- mosquitto-ssl
- libmosquitto
- lua-mosquitto

Kernel modules:
- iptables-mod-nflog
- kmod-ipt-nflog


Running:

In order for the collector to get any data, nflog rules need to be added to iptables;
- group 771 for all traffic
- group 772 for dns traffic
- group 773 for blocked traffic

For instance, to just show traffic, use
iptables -I FORWARD 0 -j nflog --nflog-group 771

The main daemon is spin_collectd.lua; this will listen on the nflog group, aggregate the result, and send it to the SPIN/traffic topic on the mosquitto server running at localhost:1883

The client is the html pages, they will connect to 192.168.8.1:1884 to listen on the SPIN/traffic topic, and display the results.


# Notes:
For the DNS output, we need a logging rule NFLOG (group 772 atm); on openwrt
iptables -I OUTPUT -p udp --source-port 53 -j NFLOG --nflog-group 772

# Changes:
- An earlier prototype used the conntrack tool, and piped its output to the aggregator, this has been replaced by the nflog implementation.
