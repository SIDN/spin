## Specification for SPIN code from kernel to usermode

#### Current situation
SPIN code currenly uses three sources of information from the system:
1) Information about (unidirectional) packetstreams
>
Per stream the following information
1) The source and destination addresses (IPv4 or IPv6)
2) The protocol (TCP or UDP)
3) The source and destination port
4) The amount of packets
5) The amount of bytes
>
2) Information about DNS requests and responses
3) The ARP cache

Current code gets the first two from a separate loadable kernel module, and the third by calling the *ip neigh* command.

SPIN code currently gives commands to kernel:
1) Commands to maintain IGNORE list
2) Commands to maintain EXCEPT list
3) Commands to maintain BLOCK list

#### Proposed functional changes

##### Arp
The ARP cache information is already from userspace, so no changes required here
##### DNS
The DNS requests and responses can be acquired from the kernel using a netfilter chain
1) Allocate a netfilter chain number
>TODO: is there a standard mechanism/registry for this? If not other software might duplicate this stream number by accident.

2)Setup capturing of interesting DNS packets on this chain
>Can be done inside or outside the running SPIN software
Basically specifying packets to/from port 53

3)Connect to the queue of this chain and decode packets just like it is now done in the kernel, or, preferably, use standard software for this
##### Packetstreams
We will make use of the Conntrack feature of Netfilter that reads accounting data.
Prerequisites are:
- Kernel modules for conntrack must be loaded
- Kernel conntrack accounting must be enabled

Conntrack data can now be read using standard netlink calls and processed

##### Commands
The commands for the IGNORE and EXCEPT lists can now functionally be handled in usermode, with a possible performance loss

The commands for the BLOCK list will have to be changed to use the standard iptable mechanism

#### Proposed other code changes
The SPIN Daemon handles all communication with the kernel and user space programs.
The code should be made more configurable, so changes in communication methods will not require changes to mainloop code.
This will increase maintainability and ease addition and/or deletion of communication.
