# Node-cache, merged nodes, UI

Various stuff about nodes in spind

## Node life
Nodes come into being by traffic
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

### TODO
Define and implement what happens to merged nodes and timed out nodes.

## Software Interfaces in spind

### Higher layers -> node  

All nodes have a persistence level, which is zero when they come to life. When something happens that makes them more or less worthy of remembering routines must be called:  

node_persistence_increase(nodenum)
node_persistence_decrease(nodenum)  

Nodes -> core2block
Node becomes persistent:
c2b_node_persistent_start(nodenum)
Node has (new) Ip address
c2b_node_ipaddress(nodenum, ip-addr)
Node stops being persistent(or disappears totally)
c2b_node_persistent_end(nodenum)

c2b_flowblock_start(nodenum1, nodenum2)
c2b_flowblock_end(nodenum1, nodenum2)
Start and ends the blocking of a flow(someone should sort the numbers)


### Reread info at reboot/restart

Currently the info saved is a file containing the pairs of nodenumbers that have their flows blocked, and the JSON description of the nodes contained in these pairs.  
Later perhaps more nodes could be saved, when they are declared persistent for other reasons.

When reading in the node descriptions the nodes will have to be allocated and get a different number. At least temporary there will need to be a mapping from old numbers to new numbers, using the tree code.

Then read the nodepair.list file and using the mapping just built make new blocked flows
