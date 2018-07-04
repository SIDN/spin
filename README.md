
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
- linux-headers-<version>

    `apt-get install gcc make autoconf libnfnetlink-dev`

Library dependencies:

- libnfnetlink0

    `apt-get install libnfnetlink0`

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
- mosquitto (for spin_mqtt.lua)


Run in the source dir:

```
    autoreconf --install
    ./configure
    make
```

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


## For OpenWRT

If you have a build environment for OpenWRT (see https://wiki.openwrt.org/doc/howto/build), you can add the following feed to the feeds.conf file:
src-git sidn https://github.com/SIDN/sidn_openwrt_pkgs

After running scripts/feeds update and scripts/feeds install, you can select the spin package in menuconfig under Network->SIDN->spin.

Running make package/spin/compile and make package/spin install will result in a spin-<version>.ipk file in bin/<architecture>.

This package has an extra file in addition to the ones described in the
previous section: a startup script /etc/init.d/spin; this script will
load the kernel module and start the spin_mqtt.lua daemon.


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
2. Load the kernel module `insmod kernelmod/spin.ko`
3. (optional) configure the kernel module with `lua/spin_config.lua`
4. Start the spin daemon `lua/spin_mqtt.lua`
5. Edit `html/js/mqtt_client.js` and change the ip address on the first line to `127.0.0.1`
6. Open `html/graph.html` in a browser



# High-level technical overview

The software contains three parts:

- a kernel module collector that reads packet traffic data
- a user-space daemon that aggregates that data, sends it to an mqtt broker, and controls the kernel module
- a html/javascript front-end for the user
- a RESTful API

The kernel module hooks into the packet filter system, and tracks packets on the forwarding and outgoing paths; it will report about this traffic on a netlink port, to which the user-space daemon can connect. The information it reports comprises three separate elements:

* Traffic: information about traffic itself, source address, destination address, ports, and payload sizes
* Blocked: information about traffic that was blocked (by SPIN, not by the general firewall)
* DNS: domain names that have been resolved into IP addresses (so the visualizer can show which domain names were used to initiate traffic)

The user-space daemon connects to the kernel module and sends the information to a local MQTT broker. It will send this information to the topic SPIN/traffic, in the form of JSON data.
The daemon will also listen for configuration commands in the topic SPIN/commands.


# Data protocols and APIs

## Kernel module to userspace programs

For the communication protocol between a user-space program and the kernel module, see [doc/netlink_protocol.md](doc/netlink_protocol.md).

## MQTT messages

For the channels and mqtt message formats, see [doc/mqtt_protocol.md](doc/mqtt_protocol.md)

## Web API

For the Web API description, see [doc/web_api.md](doc/web_api.md)

