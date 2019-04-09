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
But nodes are numbered sequentially in spind, so a persistent node will get a new number at reboot, holding the same info.
Persistent nodes will be stored in a file, rewritten at each change, removed when becoming non persistent, and read back in at reboot.


### Interfaces in spind

Higher layers -> node

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


### 