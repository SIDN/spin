
This is a prototype of the SPIN platform.

[what is SPIN]

[High-level technical overview]

[Compilation]

   [general]
   [Computer]
   [for openwrt device]

[History]



[Compilation]

The SPIN prototype is meant to be run on an OpenWRT device, but can also be compiled and run on a Linux system.

[[On (Linux) PC]]

Build dependencies:

- gcc
- make
- autoconf
- libnfnetlink-dev
- linux-headers-<version>

    apt-get install gcc make autoconf libnfnetlink-dev

Library dependencies:

- libnfnetlink0

    apt-get install libnfnetlink0

Lua dependencies (for client tooling and message broker):

- libmosquitto-dev
- lua 5.1 (and luarocks for easy install of libs below)
- lua-mosquitto
- lua-posix
- luabitop
- luaposix

    apt-get install libmosquitto-dev
    luarocks install mosquitto lua-bitop luaposix


Runtime dependencies:
- mosquitto (for spin_mqtt.lua)


Run in the source dir:
    autoreconf --install
    ./configure
    make

After this step is complete, the following files are available:
- kernelmod/spin.ko
A loadable linux kernel module
- src/spin_print
A test tool that prints messages sent by the kernel module
- src/spin_config
A simple configuration tool for the kernel module (note: there is a lua version that has more features)

The lua/ directory has a number of lua tools and programs:
- spin_mqtt.lua
This is the main SPIN daemon; it reads data from the kernel module and passes it on to the topic SPIN/traffic on the message broker running on localhost
- spin_config.lua
A config tool similar to the C version in src/, but has a few more features.
- spin_print.lua
A print test tool similar to the C version in src/.


-----
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
