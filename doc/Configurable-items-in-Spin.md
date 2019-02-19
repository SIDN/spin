#Configuration UCI/Ubus/etc
UCI is a system driven by files, and (for backwards compatibility also generating files using init.d scripts??)

In the simplest form we could use it to generate a file, say /etc/spin/configuration with all configurable items below and just read that file.
But we could also use the UCI API to query the UCI files directly.

But UCI can also be queried and changed by the Ubus RPC mechanism. The server here is *rpcd* and it is possible to get configuration items from that.

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

Add package spin in uci:, with config spind and perhaps later others


### Variables(to be completed)
config spind spind
>option iptable_debug '/tmp/blockcommands'
option iptable_queue_dns '1'
option iptable_queue_block '2'
option iptable_place_dns '0'		# At front
option iptable_place_block '0'

>option pubsub_host '127.0.0.1'
option pubsub_port '1883'
option pubsub_channel_commands 'SPIN/commands'
option pubsub_channel_traffic 'SPIN/traffic'
option pubsub_timeout '60'

>option log_usesyslog '1'
option log_loglevel '6'

### Without UCI
Make file in /etc/spin named spind.conf or so, with exactly the same names and values, so for example

 #
 # Config of Spind
 #

pubsub_port	1883

### Conf code in spind

Create a generic interface spind_conf.c or so, with entries

int spindconf_pubsub_port() { return atoi(getconf("pubsub_port")); }

and create two versions of a file that implements getconf(), one with UCI and one with files


