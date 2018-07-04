## kernel to userspace

The kernel module users netlink to talk to the user-space daemon. It
opens 2 ports: 30 for configuration commands, and 31 to send network
information traffic to.

This is the description of protocol version 1

A user-space client on the config port sends a command (see below) to
port 31, and will get one or more responses per command, ending with an
'end' response type.

A user-space client on the traffic port announces itself by sending a
netlink message (content ignored) to port 31; the connection will stay
alive as long as the client is listening. However, every once in a
while, the client must send a keep-alive ping (again, content ignored)
to let the kernel know it is still listening. The kernel module will
severely lower the amount of packets sent if it has not received a
keepalive ping after 100 messages, so as not to overflow the netlink
buffer. A recommended interval to send a keepalive ping is every 50
messages.



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

DOMAIN NAME is the the domain name in DNS wire format (RFC1035).
