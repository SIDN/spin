
This is a prototype of the SPIN platform.

# What is SPIN

SPIN stands for Security and Privacy for In-home Networks, it is a
traffic visualization tool (and in the future analyzer) intended to
help protect the home network with an eye on the Internet of Things
devices and the security problems they might bring.

Currently, the SPIN prototype is implemented as a package that can be
run on either a Linux system or an OpenWRT-based router; it can show
network activity in a graphical interface, and has the option to block
traffic on top of existing firewall functionality.

For a screenshot, see [[here]].


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
    `luarocks install mosquitto lua-bitop luaposix`


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



# High-level technical overview

The software contains three parts:

- a kernel module collector that reads packet traffic data
- a user-space daemon that aggregates that data, sends it to an mqtt broker, and controls the kernel module
- a html/javascript front-end for the user

The kernel module hooks into the packet filter system, and tracks packets on the forwarding and outgoing paths; it will report about this traffic on a netlink port, to which the user-space daemon can connect. The information it reports comprises three separate elements:

* Traffic: information about traffic itself, source address, destination address, ports, and payload sizes
* Blocked: information about traffic that was blocked (by SPIN, not by the general firewall)
* DNS: domain names that have been resolved into IP addresses (so the visualizer can show which domain names were used to initiate traffic)

The user-space daemon connects to the kernel module and sends the information to a local MQTT broker. It will send this information to the topic SPIN/traffic, in the form of JSON data.
The daemon will also listen for configuration commands in the topic SPIN/commands.


# Data protocols

## kernel to userspace

The kernel module users netlink to talk to the user-space daemon. It
opens 2 ports: 30 for configuration commands, and 31 to send network
information traffic to.

This is the description of protocol version 1


### Configuration commands

Configuration commands are of the form


                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |   PROTOCOL VERSION    |       COMMAND         |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |       ARG TYPE        |                       |
    |--+--+--+--+--+--+--+--+                       |
    |                                               |
    /                                               /
    /                  ARG DATA                     /
    /                                               /
    /                                               /
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

The PROTOCOL VERSION value is 1.

COMMAND is one of:

* SPIN_CMD_GET_IGNORE = 1
* SPIN_CMD_ADD_IGNORE = 2
* SPIN_CMD_REMOVE_IGNORE = 3
* SPIN_CMD_CLEAR_IGNORE = 4
* SPIN_CMD_GET_BLOCK = 5
* SPIN_CMD_ADD_BLOCK = 6
* SPIN_CMD_REMOVE_BLOCK = 7
* SPIN_CMD_CLEAR_BLOCK = 8
* SPIN_CMD_GET_EXCEPT = 9
* SPIN_CMD_ADD_EXCEPT = 10
* SPIN_CMD_REMOVE_EXCEPT = 11
* SPIN_CMD_CLEAR_EXCEPT = 12

Each command either retrieves, adds to, remove from, or clears one of
the three internal lists (ignore, block or except).

Currently, ARG_TYPE is either AF_INET or AF_INET6, and depending on
that, ARG DATA is either 4 or 16 bytes of data.


### Configuration response

                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |   PROTOCOL VERSION    |       RESPONSE        |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    |                                               |
    |                                               |
    /                                               /
    /                  ARG DATA                     /
    /                                               /
    /                                               /
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

The PROTOCOL VERSION value is 1.

RESPONSE is one of:

* SPIN_CMD_IP = 100
* SPIN_CMD_END = 200
* SPIN_CMD_ERR = 250

ARG DATA depends on the RESPONSE value:
In the case of SPIN_CMD_IP, ARG DATA will contain one byte with the value of either AF_INET or AF_INET6 , followed by 4 or 16 bytes of address data.
In the case of SPIN_CMD_END there is no argument data (this signals the transmission is over).
In the case of SPIN_CMD_ERR, ARG DATA will be a string with an error message.

Currently, ARG_TYPE is either AF_INET or AF_INET6, and depending on
that, ARG DATA is either 4 or 16 bytes of data.

### traffic message

TODO


## MQTT message formats

### Traffic information

* Traffic
* DNS information


### Configuration commands
