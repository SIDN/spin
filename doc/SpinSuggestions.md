#Spin Suggestions for TODO
Some stuff that seems useful

***

## Issues with node info (on Mosquitto traffic channel)

Basically they grow too big

### Nodes IP address lists

Nodes are collections of info of the same host. In a sense they are clouds of IP addresses and domain names that are deemed to be the same

Especially with CDN nodes this can grow quite big. Two nodes with two lists of IP addresses will be merged into one if only one of the IP addresses overlap.

This can be good or not, but it leads to a problem.
The messages on the traffic channel, when presenting a node(maybe in a set of flows) will give all the information of a node, and this can be very large. We have already seen nodes with 200 IP addresses in short tests. Every time this node passes all this information gets on the channel. Sometimes the same node occurs more than once in one message, and even there all the information is duplicated.

### A possible solution
We separate the information about nodes from the information that mentions the nodes. Basically instead of saying:

	Hey, I saw traffic from node A(IP:blah blah, domain blah blah) to node B(blah blah)

We could say:

	Node A(blah blah)
	Node B(Blah)
	Hey I saw traffic from Node A to Node B
	
and a further optimization could be to only send the information about the nodes if it changed from last time, or a certain amount of time has passed. This would be an optimization similar to video compression.

***

## Statistics

To get an idea what the Spin Daemon is doing it would seem handy to add some statistics counters to the code, and a way to query/report them

- Allocated/freed data structures
- Packets going through
- MQTT messages going through
- Maximum node size(maybe buckets??)


## UI and Mosquitto

The commands need to be cleaned up for new names etc.
Perhaps a SPIN/statistics channel?

## Spind code

List maintenance code in separate file?
Better separation of Spin vs MQTT vs JSON code
Destroy nodes after long idle time
