#Configurable items in Spin

All configurable items found so far. They will be marked *Italic* if they are runtime modifyable.

All these should go out of the code, and into UCI/conffiles or ubus, or both.

##Per file itemization
This should change of course

### core2block.c
Debug file for iptables commands
Place in tables where to insert?
IPtables queue number

### core2kernel.c

**Not done, file should go**
### File for DNS stuff
Where in iptables to insert

### core2pubsub.c

MQTT channels for commands and traffic
Mosquitto keepalive time

### spind.c

Mosquitto host and port

### spin_config.c
*Lists of ip-addresses*