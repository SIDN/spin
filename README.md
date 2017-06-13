
This is a prototype of the SPIN platform.

[what is SPIN]

[High-level technical overview]

[Compilation]

   [general]
   [Computer]
   [for openwrt device]

[History]


The software contains three parts

- a collector that reads conntrack output (through a named pipe) and aggregates it. It serves the aggregated data on a websocket
- a few html pages to process that data


Prerequisites:
Software:

Kernel modules:
- nf_conntrack
- nf_conntrack_ipv4
- nf_conntrack_ipv6

(not right now, but soon we'll need _acct as well, by setting
the sysctl setting net.netfilter.nf_conntrack_acct to 1)


Running:

make sure conntrack has output (try sudo conntrack -E for a bit)
run ./test_server.sh
This creates a named pipe, starts conntrack (will ask for sudo password), and start the websocket server.
Then open html/print.html in a browser to see data is getting through.
For the visual representation, open html/graph.html.

# Notes:
For the DNS output, we need a logging rule NFLOG (group 1 atm); on openwrt
we add these lines to /etc/firewall.user:
iptables -I OUTPUT -p udp --source-port 53 -j NFLOG --nflog-group 1
