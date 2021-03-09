# Flow statistics in spind

Keeping track of a moderate amount of flow statistics in spind is possible, but it requires new code and data-structures.  

Issues:
- Keeping track of all nodepairs for a certain amount of time or flows will possibly flush flows of quiter nodes by talkative nodes.
- Keeping track of a certain amount of flows per node will not have this problem, but the nodes they talked to might be gone. This could be solved by a certain type of persistence. If a certain node is used in other information do not do timeout on it.
- Keeping track of nodenumbers in general needs care when merging nodes.


## Implementation

### Nodes and devices

We introduced the concept of local devices. Local devices are supposed to be devices in-home, on one or more VLAN's. We will make nodes be slightly different when they are also a device. Currently a node with an address in the ARP table becomes a device.

A node that is also a device has some extra information associated with it. Currently we keep information about flows.

Per flow we keep the following
- Blocked or not
- Number of packets
- Number of bytes
- Idle periods
- Active last period

For each node in the flow table we increase a counter for the node, to record a sort of persistency.
Nodes with a non-zero counter here are not discarded due to age, but might still be merged.

Every FLOW_TIME seconds(configurable, about a minute?), we run through the flows.
- If non active last period increase idle
- If active run idle through decreasing function(half, set to zero, whatever)
- If idle > FLOW_MAX_IDLE1(configurable, about 10 minutes?) look at number of flows and if larger than NODE_KEEP_FLOWS(configurable, about 10?) clean up flow(maybe with probability < 1 ?)
- Maybe later: If idle > FLOW_MAX_IDLE2(configurable, about two weeks?) clean up flow

This will keep a certain minimum number of flows per node, but if larger will start removing them, and clean up regardless after a long time.

Nodes will have two levels of reference counts, reference and persistence. Each data structure in *spind* that points to a node(either by pointer of use of number) will increase the reference counter. Nodes with a non-zero reference count will not be discarded due to non-use. The other data-structures become responsible for cleaning up the reference.

When nodes with non-zero reference counts are merged into another node the other datastructures must be called to  cleanup.

The persistent count will guarantee the node informnation will be saved across restarts of the spind and reboots.

Currently traffic from device to device will be counted both ways.
We are not likely to see this happen soon.

### Flows and RPC's

There are RPC's, currently implemented as JSON-RPC, to look at devices and their flows.

The first one is get_devices. 

	{
 	"jsonrpc": "2.0",
  	"id": 82273,
 	"method": "get_devices",
  	"params": ""
	}

Giving as output:  

	{
	"jsonrpc": "2.0",
	"id": 82273,
	"result": [
		{
	     "id": 7062,
	     "name": "1040-04-090",
	     "mac": "00:50:b6:6a:b3:70",
	     "lastseen": 1557900262,
	     "ips": [
	        "192.168.8.217"
	      ],
	      "domains": []
	    },
	    {
	     "id": 103,
	     "name": "iPhone-van-Hans",
	     "mac": "c0:e8:62:2d:cc:12",
	     "lastseen": 1557843177,
	     "ips": [
	        "192.168.8.175"
	      ],
	      "domains": []
	   }
	  ]
	}

and the second one is get_deviceflow:

	{
	 "jsonrpc": "2.0",
	 "id": 82542,
	 "method": "get_deviceflow",
	 "params": {
	 	"device": "c0:e8:62:2d:cc:12"
	 }
	}

giving:

	{
	  "jsonrpc": "2.0",
	  "id": 82542,
	  "result": [
	    {
	      "to": 16,
	      "packets": 879,
	      "bytes": 125174
	    },
	    {
	      "to": 6095,
	      "packets": 53,
	      "bytes": 19271
	    },
	    {
	      "to": 6100,
	      "packets": 1213,
	      "bytes": 449847
	    },
	    {
	      "to": 6994,
	      "packets": 42,
	      "bytes": 14425
	    },
	    {
	      "to": 6999,
	      "packets": 28,
	      "bytes": 6495
	    }
	  ]
	}


The next step is to implement the RPC to block flows as taking a device(MAC address) and a node as argument for consistency.

