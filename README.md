
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

The PROTOCOL VERSION (1 byte) value is 1.

COMMAND (1 byte) is one of:

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

The PROTOCOL VERSION (1 byte) value is 1.

RESPONSE (1 byte) is one of:

* SPIN_CMD_IP = 100
* SPIN_CMD_END = 200
* SPIN_CMD_ERR = 250

ARG DATA depends on the RESPONSE value:
In the case of SPIN_CMD_IP, ARG DATA will contain one byte with the value of either AF_INET or AF_INET6 , followed by 4 or 16 bytes of address data.
In the case of SPIN_CMD_END there is no argument data (this signals the transmission is over).
In the case of SPIN_CMD_ERR, ARG DATA will be a string with an error message.

Currently, ARG_TYPE is either AF_INET or AF_INET6, and depending on
that, ARG DATA is either 4 or 16 bytes of data.

### traffic messages

                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |    PROTOCOL VERSION   |         TYPE          |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     SIZE                      |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    /                     DATA                      /
    /                                               /
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

PROTOCOL VERSION (1 byte) has the value 1.

TYPE (1 byte) can be one of SPIN_TRAFFIC_DATA (1), SPIN_DNS_ANSWER (2),
SPIN_BLOCKED (3), SPIN_ERR_BADVERSION (250)

SIZE (2 bytes) is the total size of this message

The DATA section depends on the type, for SPIN_TRAFFIC_DATA and
SPIN_BLOCKED_DATA it contains IP packet information (see 'Plain and
blocked traffic'), for SPIN_DNS_ANSWER it contains domain name
information, see 'DNS Answer'. In the case if SPIN_ERR_BADVERSION, the
DATA section is empty; this message signals there is a version conflict
and the module does not know what to send.


#### Plain and blocked traffic data

                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |        FAMILY         |       PROTOCOL        |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    |                                               |
    |                                               |
    |                    SOURCE                     |
    |                    ADDRESS                    |
    |                                               |
    |                                               |
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    |                                               |
    |                                               |
    |                  DESTINATION                  |
    |                    ADDRESS                    |
    |                                               |
    |                                               |
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                  SOURCE PORT                  |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |               DESTINATION PORT                |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                 PACKET COUNT                  |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                 PAYLOAD SIZE                  |
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                PAYLOAD OFFSET                 |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

FAMILY (1 byte) is either AF_INET or AF_INET6

PROTOCOL (1 byte) is the IP protocol number of the packet(s)

SOURCE ADDRESS and DESTINATION ADDRESS (both 16 bytes) contain the IP
addresses; in the case of IPv4, the first 12 bytes are 0, and the IP
address is in the last 4.

SOURCE PORT and DESTINATION PORT (both 2 bytes) contain the 16-bit port
numbers.

PACKET COUNT (2 bytes) contains the number of packets that were seen in
the last time interval (1 second).

PAYLOAD SIZE (4 bytes) is the total payload size of the packets that
were seen.

PAYLOAD OFFSET (2 bytes) contains the offset of the packet payload;
i.e. the header length. This is mainly used internally; it is only set
for the first packet that was seen in this time interval, and the
packet payload itself is not transfered anyway.

### DNS info message

                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |        FAMILY         |                       |
    +--+--+--+--+--+--+--+--+                       |
    |                                               |
    |                                               |
    |                      IP                       |
    |                    ADDRESS                    |
    |                                               |
    |                       +--+--+--+--+--+--+--+--+
    |                       |                       |
    +--+--+--+--+--+--+--+--+
    |                     TTL                       |
    |                       +--+--+--+--+--+--+--+--+
    |                       |   DOMAIN NAME SIZE    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    /                 DOMAIN NAME                   /
    /                                               /
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

FAMILY contains the IP address family (either AF_INET or AF_INET6).

IP ADDRESS (16 bytes) contains the IP address found in the DNS answer;
in the case of IPv4, the first 12 bytes are 0, and the address is in
the last 4.

TTL (4 bytes) contains the Time-to-live for the address record, as found
in the DNS ansewr.

DOMAIN NAME SIZE (1 byte) is the size of the domain name.

DOMAIN NAME is the string-representation of the domain name.

## MQTT message formats

The spin_mqtt.lua daemon will send traffic infromation to the topic
SPIN/traffic. It will listen on the topic SPIN/commands for commands.

All messages are of the format

    {
       "command": <command name (string)>,
       "argument": <argument(s) (dynamic)>,
       "result": <result(s) (dynamic)>
    }

On the SPIN/traffic topic, commands can be one of:

* "traffic": Traffic flow information, see next section
* "nodeUpdate": Updates information about nodes that were seen earlier
  (such as an additional domain name that resolved to the same IP
  address, or a new IP address that is matched to an ARP address that has
  been seen before)

### Traffic information

The 'result' section of traffic information is a map containing the
following elements:

* "timestamp": (int) timestamp of the first message seen in this time interval
* "total_size": (int) total size of the packets seen in this time interval
* "total_count": (int) total number of packets seen in this time interval
* "flows": A list of flows.

Each flow element is a map containing the following elements:

* "size": (int) Size of the packets in this flow
* "count": (int) Number of packets in this flow
* "from": Information about the source of the traffic
* "to": Information about the destination of the traffic

The 'from' and 'to' elements represent more than single addresses; they
contain all information known about the source or destination; in this
context we call it a node. A node is represented by a map containing
the following information:

* "id": (int) internal identifier for the node
* "lastseen": (int) timestamp of the last time this node was seen in the traffic data
* "ips": (list of strings) A list of the addresses known to belong to this node (in string format)
* "domains": (list of strings) A list of the domain names known to resolve to one of the addresses of this node
* "mac": (string, optional) MAC address that for this node, if relevant and known


### Example Traffic information


Here is an example of a full traffic message:

    {
       "command":"traffic",
       "argument":"",
       "result":{
          "flows":[
             {
                "size":16,
                "count":1,
                "to":{
                   "id":10,
                   "lastseen":1497622511,
                   "ips":[
                      "ff02::2"
                   ],
                   "domains":[

                   ]
                },
                "from":{
                   "mac":"b4:75:0e:15:39:9d",
                   "ips":[
                      "fe80::b675:eff:fe15:399d"
                   ],
                   "id":9,
                   "domains":[

                   ],
                   "lastseen":1497622511
                }
             },
             {
                "size":101,
                "count":1,
                "to":{
                   "id":12,
                   "lastseen":1497622512,
                   "ips":[
                      "2001:470:1f15:17ba::53"
                   ],
                   "domains":[

                   ]
                },
                "from":{
                   "id":11,
                   "lastseen":1497622512,
                   "ips":[
                      "2a02:348:28:cf34::1"
                   ],
                   "domains":[

                   ]
                }
             }
          ],
          "timestamp":1497622538,
          "total_size":117,
          "total_count":2
       }
    }


* Traffic
* DNS information


### Configuration commands
