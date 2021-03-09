# Node-cache, merged nodes, UI

Various stuff about nodes in spind. Blocked flows, persistent nodes, implementation using ipsets, ...

## Node life
Nodes come into being by being observed in traffic
They grow over time with IP-addresses and domains
They can disappear by being merged into other nodes, or by lack of traffic for a long time

Node info is published with retained message on their own channel
What to do with nodes disappearing for one of two reasons?
- Nodes being merged: publish new node-id as only info
- Nodes being timed out??

Nodes can become persistent if they need to be remembered over reboots. Currently only because they are in blocked flows.  
But nodes are numbered sequentially in spind, so a persistent node  gets a new number at reboot, holding the same info.
Persistent nodes are stored in a file in JSON format, rewritten at each change, removed when becoming non persistent, and read back in at reboot.

Also when nodes change their list of IP addresses can have grown, so lower layers(core2block) are informed of that.

### *TODO*
Define and implement what happens to merged nodes and timed out nodes.

## Software Interfaces in spind

### RPC layer
Currently implemented using Ubus. Discussion needed here, about software(marshalling, JSON, etc)

	root@OpenWrt:~# ubus -v list spin
	'spin' @775c9678
	"spindlist":{"list":"Integer","addrem":"Integer","node":"Integer"}
	"blockflow":{"node1":"Integer","node2":"Integer","block":"Integer"}
	"get_blockflow":{}

It is now pretty easy to add more RPC calls, although the Ubus RPC glue code is a bit larger than I would like.

Blockflow gets two node numbers(order irrelevant) and a boolean(block/unblock) and implements blocking.  
Get_blockflow returns an array of node pairs.

### Higher layers -> node  

All nodes have a persistence level, which is zero when they come to life. When something happens that makes them more or less worthy of remembering routines must be called:  

node_persistence_increase(nodenum)
node_persistence_decrease(nodenum)  

### Nodes -> core2block

Currently about once per second modified nodes are examined:
-  They are published on their own MQTT channel
- If they are, or just became, persistent they are written to file
- Their list of IP addresses is sent (again) to lower layers

Node becomes persistent(persistence level goes up from zero0:  

c2b_node_persistent_start(nodenum)  
Node stops being persistent(or disappears totally):  
c2b_node_persistent_end(nodenum)  

Node has (new) Ip address, this function is called for all initial IP addresses when a node becomes persistent, and also for all IP addresses when a node is modified. So the same address can be added multiple times, the c2b_node_ipaddress function must be able to handle this.

c2b_node_ipaddress(nodenum, ip-addr)

Functions to start and end   
c2b_blockflow_start(nodenum1, nodenum2)  
c2b_blockflow_end(nodenum1, nodenum2)  
Start and ends the blocking of a flow((nodenum1 < nodenum2)

#### core2block implementation details
  
When a node becomes persistent two ipsets are created for the node, one for the IPv4 addresses, and one for the IPv6 addresses.  
These ipsets are used for blocking the flow between this node and another. 
Because of the existence of V4 and V6 sets of addresses, and the current symmetry in blocking rules this needs 2x2=4 iptables rules.

### Reread info at reboot/restart

Currently the info saved is a file containing the pairs of nodenumbers that have their flows blocked, and the JSON description of the nodes contained in these pairs.  
Later perhaps more nodes could be saved, when they are declared persistent for other reasons.

When reading in the node descriptions nodes are allocated allocated and given a new (different) number. There is a temporary mapping from old numbers to new numbers, using the tree code.  
Then read the nodepair.list file and using the mapping just built make new blocked flows.  
After reading everything back in the temporary mapping, and all previous administration is deleted.

A potential issue here is that during startup both the old and new files must coexist. The nodes are read first, and entered without being persistent, so they are not written back immediately.
But while reading the file of nodepairs for blocking the code to block pairs is called, and will overwrite the file. To prevent this problem the code unlinks the nodepair file directly after opening, so a new file will be created. This works without a problem on Linux variants, but could become a problem when porting to very different systems.

### Effects on user interfaces

To implement the flow blocking there has to be a way to point to a flow and delete.
