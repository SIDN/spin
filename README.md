
This is a prototype of the SPIN platform.

[what is SPIN]

TODO

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


[[For OpenWRT]]

If you have a build environment for OpenWRT (see https://wiki.openwrt.org/doc/howto/build), you can add the following feed to the feeds.conf file:
src-git sidn https://github.com/SIDN/sidn_openwrt_pkgs

After running scripts/feeds update and scripts/feeds install, you can select the spin package in menuconfig under Network->SIDN->spin.

Running make package/spin/compile and make package/spin install will result in a spin-<version>.ipk file in bin/<architecture>.

This package has an extra file in addition to the ones described in the
previous section: a startup script /etc/init.d/spin; this script will
load the kernel module and start the spin_mqtt.lua daemon.



[High-level technical overview]

The software contains three parts:

- a kernel module collector that reads packet traffic data
- a user-space daemon that aggregates that data, sends it to an mqtt broker, and controls the kernel module
- a html/javascript front-end for the user

The kernel module

[Data protocols]

[[kernel to userspace]]

The kernel module users netlink to talk to the user-space daemon. It
opens 2 ports: 30 for configuration commands, and 31 to send network
information traffic to.

This is the description of protocol version 1

[[Configuration commands]]
Configuration commands are of the form


+-----
