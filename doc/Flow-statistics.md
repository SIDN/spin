# Flow statistics in spind

Keeping track of a moderate amount of flow statistics in spind is possible, but it requires new code and data-structures.  

Issues:
- Keeping track of all nodepairs for a certain amount of time or flows will possibly flush flows of quiter nodes by talkative nodes.
- Keeping track of a certain amount of flows per node will not have this problem, but the nodes they talked to might be gone. This could be solved by a certain type of persistence. If a certain node is used in other information do not do timeout on it.
- Keeping track of nodenumbers in general needs care when merging nodes.

## Proposal

### Flows
We keep a list of flows, indexed by src/dst pairs.
Per flow we keep the following
- Blocked or not
- Number of packets
- Number of bytes
- Idle periods
- Active last period

For each node in the flow table we increase a counter for the node, to record a sort of persistency.
Nodes with a non-zero counter here are not discarded due to age, but might still be merged

Every FLOW_TIME seconds(configurable, about a minute?), we run through the flows.
- If non active last period increase idle
- If active run idle through decreasing function(half, set to zero, whatever)
- If idle > FLOW_MAX_IDLE1(configurable, about 10 minutes?) look at max(persist(node1), persist(node2), and if larger than NODE_KEEP_FLOWS(configurable, about 25?) clean up flow(maybe with probability < 1 ?)
- If idle > FLOW_MAX_IDLE2(configurable, about 30 minutes?) clean up flow

This will keep a certain minimum number of flows per node, but if larger will start removing them, and clean up regardless after a long time.

### Nodes
Also periodically(same FLOW_TIME?) go through node cache, sort of like we do now. If persistency number is zero, meaning no flow is kept for this node anymore, node could be deleted. Even then we may keep it around for some periods(configurable?), not to lose info if it comes back.
But eventually it has to go.

When merging nodes we have to go through the flow table to also merge corresponding flows. We have to set values reasonable. What when one of the flows turns out to be blocked, and the other one isn't?



