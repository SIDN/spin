
This is a prototype of the SPIN platform.

# What is SPIN?

SPIN stands for Security and Privacy for In-home Networks, it is a
traffic visualization tool (and in the future analyzer) intended to
help protect the home network with an eye on the Internet of Things
devices and the security problems they might bring.

Currently, the SPIN prototype is implemented as a package that can be
run on either a Linux system or an OpenWRT-based router; it can show
network activity in a graphical interface, and has the option to block
traffic on top of existing firewall functionality.

For a screenshot, see [here](/doc/images/prototype-20170103.png?raw=true).


# Building the source code

The SPIN prototype is meant to be run on an OpenWRT device, but can also be compiled and run on a Linux system.

## On (Linux) PC

Build dependencies:

- gcc
- make
- autoconf
- libnfnetlink-dev
- linux-headers-&lt;version&gt;

    `apt-get install gcc make autoconf libnfnetlink-dev libmnl-dev libnetfilter-queue-dev`

Library dependencies:

- libnfnetlink0
- libmnl

    `apt-get install libnfnetlink0 libmnl0`

Lua dependencies (for client tooling and message broker):

- libmosquitto-dev
- lua 5.1 (and luarocks for easy install of libs below)
- lua-mosquitto
- lua-posix
- luabitop
- luaposix

    `apt-get install libmosquitto-dev`
    `luarocks install lua-mosquitto luabitop luaposix`


Runtime dependencies:
- mosquitto (or any MQTT software that supports websockets as well)


Run in the source dir:

```
    autoreconf --install
    ./configure
    make
```

After this step is complete, you can find the spin daemon in the spind directory. The tools/spin_print and tools/spin_config tools are supporting tools for older versions and deprectaed in the latest release.

SPIN sends its data to MQTT, which any MQTT client can then read. A web-based client ('the bubble app') can be found in `src/web_ui/static/spin_api/`, the main HTML file is `graph.html`, and depending on where you host it, you can access it from a browser with the URL `file://<path>/graph.html?mqtt_host=<ip address of MQTT server>`.

src/web_ui also contains a small web API server that uses lua-minithttp, with currently a limited subset of the intended functionality; we are working on RPC calls that will then be exposed to the web API.

## For OpenWRT

If you have a build environment for OpenWRT (see https://wiki.openwrt.org/doc/howto/build), you can add the following feed to the feeds.conf file:
src-git sidn https://github.com/SIDN/sidn_openwrt_pkgs

After running scripts/feeds update and scripts/feeds install, you can select the spin package in menuconfig under Network->SIDN->spin.

Running make package/spin/compile and make package/spin install will result in a spin-<version>.ipk file in bin/<architecture>.

This package has an extra file in addition to the ones described in the
previous section: a startup script /etc/init.d/spin; this script will
load the kernel module and start the spin_mqtt.lua daemon.

Please keep in mind that for the 'bubble-app' front-end, you will also need to install a webserver, and configure it to serve the pages installed by the package in /usr/lib/spin/web_ui/static.


# Running SPIN

(NOTE: in the current version, the IP address of the router/server is
hardcoded to be 192.168.8.1; this is also the address used in this
example. We are working on deriving this address automatically, or at
the very least make it a configuration option).

When the OpenWRT package is installed, SPIN should start automatically
after a reboot. Simply use a browser to go to
http://192.168.8.1/www/spin/graph.html to see it in action.

When installed locally, a few manual steps are required:

1. Configure and start an MQTT service; this needs to listen to port 1883 (mqtt) and 1884 (websockets protocol).
2. Load the relevant kernel modules: `modprobe nf_conntrack_ipv4 nf_conntrack_ipv6 nfnetlink_log nfnetlink_queue`
3. Enable conntrack accounting: `sysctl net.netfilter.nf_conntrack_acct=1`
4. Start the spin daemon `spind/spind -l -o -m <mqtt_host>`
5. Load the spin bubble app by visiting `file::///<path>/web_ui/static/spin_api/graph.html?mqtt_host=<mqtt host>`

mqtt host defaults to 127.0.0.1 for the daemon, and to 192.168.8.1 for graph.html

# High-level technical overview

The software contains three parts:

- a daemon that aggregates traffic and DNS information (with nf_conntract and nflog) and sends it to MQTT
- a html/javascript front-end for the user
- a RESTful API (currently only a subset of the intended funcitonality)

The information that is sent to MQTT contains the following types:
* Traffic: information about traffic itself, source address, destination address, ports, and payload sizes
* Blocked: information about traffic that was blocked (by SPIN, not by the general firewall)
* DNS: domain names that have been resolved into IP addresses (so the visualizer can show which domain names were used to initiate traffic)

The daemon will also listen for configuration commands in the MQTT topic SPIN/commands. This will be replaces by RPC calls in the near future.


# Data protocols and APIs

## MQTT messages

For the channels and mqtt message formats, see [doc/mqtt_protocol.md](doc/mqtt_protocol.md)

## Web API

For the Web API description, see [doc/web_api.md](doc/web_api.md)

