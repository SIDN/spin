
This is an early prototype of the SPIN platform.

As of right now, it consists of two very basic modules:
- a collector that reads conntrack output (through a named pipe) and aggregates it. It serves the aggregated data on a websocket
- a few html pages to process that data


Prerequisites:
Software:
- conntrack binary
- luaposix
- luasec
- luabitop
- lua-coxpcall
- lua-copas
- lua-socket

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

