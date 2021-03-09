# Code cleanup in spind.c

### Cleaner code with lists
The code maintains three lists, the block, ignore and allow list. All lists contain IP addresses. The code used to handle all lists separately but now they are handled roughly the same. The lists are numbered 0,1 and 2, for IPLIST_BLOCK, IPLIST_IGNORE and IPLIST_ALLOW. The boolean fields in the node_cache code for is_blocked and is_allowed are now aliases for one of three array fields, one per list.

All calls dealing with lists are now the same, just specifying the list by number. The semantic differences are all handled in the code below.
###New files
There are three new files, core2kernel.c, core2pubsub.c and core2block.c

The idea is to separate the core code(list maintenance, etc) from the interfaces to the world
- The core2kernel file implements the interface to the old kernel module using the netlink protocol
- The core2pubsub file implements the Mosquitto interface to (among others) the bubble web application
- The core2block file implements the new blocking code using iptables

### Code hooks
All interfaces can attach to the core code by two registration functions:

1. The mainloop_register function. This can be called with a file descriptor and a timeout and will call back a function when either the filedescriptor has data or the timeout went off
2. The spin_register function. This can be called with an array of three integers(which lists am I interested in?) and will call back when one of the interesting lists has an entry added or deleted.

Multiple subsystems can register with one or more of these registration functions.

Initialization functions still need to be called from spind.c, in theory we could setup some sort of /etc/rc.d type of startup/shutdown.

