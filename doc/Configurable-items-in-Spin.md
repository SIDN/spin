#Configuration UCI/Ubus/etc
UCI is a system driven by files, and (for backwards compatibility also generating files

In the simplest form we could use it to generate a file, say /etc/spin/configuration with all configurable items below

But UCI also generates a datastructure that can be queried and changed by the Ubus RPC mechanism. The server here is *rpcd* and it is possible to get configuration items from that.

One way we could move is to isolate the configuration query mechanism into a separate file, and at build time choose between the file version and the Ubus version. This should be easy to implement and test.

Ubus itself looks to be a relatively simple RPC mechanism. It is one to one, and on the wire it uses simple TLV items. Normal interface is with a library that uses a Unix socket, although the documentation suggests it can be used over IP(not found yet). There are userspace programs to deal with Ubus too, and these all use JSON formatted data, converting to/from TLV if needed.

Ubus datatypes are int's of various sizes, strings, and array/table structures. The latter seem vary simple.

#Configurable items in Spin

All configurable items found so far. They will be marked *Italic* if they are runtime modifyable.

All these should go out of the code, and into UCI/conffiles or ubus, or both.

##Per file itemization
This should change of course

### core2block.c
Debug file for iptables commands
Place in tables where to insert?
IPtables queue number

### core2nfq_dns
DNS queue number

### core2kernel.c

**Not done, file should go**
### File for DNS stuff
Where in iptables to insert

### core2pubsub.c

MQTT channels for commands and traffic
Mosquitto keepalive time

### spind.c

Mosquitto host and port
Logging settings

### spin_config.c
*Lists of ip-addresses*

###Various
At various places more or less random timers are used. These could be configured.

# Potential UCI structure

Add section spin in uci:

Add variables of type int32 and type string

### Variables(to be completed)
spin.iptable.debugfile	*value* "/tmp/blockcommands"
spin.iptable.queuedns	*value* 1
spin.iptable.queueblock	*value* 2

spin.iptableplace.dns	*value* 0
spin.iptableplace.block	*value* 0

spin.pubsub.host *value* "name/address"
spin.pubsub.port *value* 1883
spin.pubsub.channel.command	*value* "SPIN/traffic"
spin.subsub.channel.traffic	*value*	"SPIN/commands"
spin.pubsub.timeout	*value*	60

spin.log.usesyslog	*value*	1
spin.log.loglevel	*value*	6



