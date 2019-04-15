# mqtt protocol of Spind

The Spindaemon will send traffic information to the topic
SPIN/traffic. It will listen on the topic SPIN/commands for commands.

All messages are of the format

    {
       "command": <command name (string)>,
       "argument": <argument(s) (dynamic)>,
       "result": <result(s) (dynamic)>
    }

On the SPIN/traffic topic, commands can be one of:

* "traffic": Traffic flow information, see next section

* "serverRestart": Tells the client that the server has restarted and
  it should drop its local cache of nodes. argument and result are empty.

* "blocks" result is a list of IP addresses that are blocked

* "allows" result is a list of IP addresses that are immune to having their traffic blocked

* "ignores": result is a list of strings containing IP addresses that are
  currently filtered (not shown) by the SPIN system.

* "names": result is a map containing IP address -> domain name values;
  these are user-set names.

* "peakinfo": lists information for a specific node from the peak-based 
  anomaly detection, if enabled
  
* "names": result is a map containing IP address -> name values;
  these are user-set names. **Currently not implemented.**

** Proposed changes are documented here **

### Traffic information

The 'result' section of traffic information is a map containing the
following elements:

* "timestamp": (int) timestamp of the first message seen in this time interval
* "total_size": (int) total size of the packets seen in this time interval
* "total_count": (int) total number of packets seen in this time interval
* "flows": A list of flows.

Each flow element is a map containing the following elements:

* "size": (int) Size of the packets in this flow
* "count": (int) Number of packets in this flow
* "from": Information about the source of the traffic
* "to": Information about the destination of the traffic

The 'from' and 'to' elements represent more than single addresses; they
contain all information known about the source or destination; in this
context we call it a node. A node is represented by a map containing
the following information:

* "id": (int) internal identifier for the node
* "lastseen": (int) timestamp of the last time this node was seen in the traffic data
* "ips": (list of strings) A list of the addresses known to belong to this node (in string format)
* "domains": (list of strings) A list of the domain names known to resolve to one of the addresses of this node
* "mac": (string, optional) MAC address that for this node, if relevant and known


### Example Traffic information


Here is an example of a full traffic message:

    {
       "command":"traffic",
       "argument":"",
       "result":{
          "flows":[
             {
                "size":16,
                "count":1,
                "to":{
                   "id":10,
                   "lastseen":1497622511,
                   "ips":[
                      "ff02::2"
                   ],
                   "domains":[
                      "www.example.nl"
                   ]
                },
                "from":{
                   "mac":"e4:11:e0:1a:11:6b",
                   "ips":[
                      "fe80::b123:1fa:1e15:329a"
                   ],
                   "id":9,
                   "domains":[

                   ],
                   "lastseen":1497622511
                },
                "protocol": 6,
                "from_port": 53025,
                "to_port": 443
             },
             {
                "size":101,
                "count":1,
                "to":{
                   "id":12,
                   "lastseen":1497622512,
                   "ips":[
                      "2001:170:2a00:11be::12"
                   ],
                   "domains":[

                   ]
                },
                "from":{
                   "id":11,
                   "lastseen":1497622512,
                   "ips":[
                      "2a02:249:13:ac43::1"
                   ],
                   "domains":[

                   ]
                },
                "protocol": 6,
                "from_port": 4242,
                "to_port": 80
             }
          ],
          "timestamp":1497622538,
          "total_size":117,
          "total_count":2
       }
    }

After the proposed changes:

    {
       "command":"traffic",
       "argument":"",
       "result":{
          "flows":[
             {
                "size":16,
                "count":1,
                "to": 10,
                "from": 9,
                "protocol": 6,
                "from_port": 53025,
                "to_port": 443
             },
             {
                "size":101,
                "count":1,
                "to": 12,
                "from":11,
                "protocol": 6,
                "from_port": 4242,
                "to_port": 80
             }
          ],
          "timestamp":1497622538,
          "total_size":117,
          "total_count":2
       }
    }
### Node information

A node update contains the same information as a node element from the
previous section; it contains (additional) information about a node
that has been seen earlier.

** Proposed change is to re-add this, with different name. nodeInfo**

### Node update example

    {
       "command":"nodeInfo",
       "argument":"",
       "result":{
          "id":11,
          "lastseen":1497623925,
          "ips":[
             "192.0.2.1"
          ],
          "domains":[
             "example.com",
             "example.nl"
          ]
       }
    }

Nodes can disappear for two reasons:
- They are merged into another node, effectively joining the information about the two nodes
- They can disappear because of a timeout

These disappearances have to published:

    {
       "command":"nodeMerge",
       "argument":"",
       "result":{
          "id":11,
          "merged-to":173
       }
    }
    
    {
       "command":"nodeDeleted",
       "argument":"",
       "result":{
          "id":11,
       }
    }
    
### Configuration commands

The client can send commands to the SPIN daemon on the 'SPIN/commands' topic. These usually have no "result" value, but often do contain an "argument" section.

The following commands can be issued:
	
	"get_blocks": Triggers a new "blocks" message to be sent to SPIN/traffic
	"get_ignores": Triggers a new "ignores" message to be sent to SPIN/traffic
	"get_alloweds": Triggers a new "alloweds" message to be sent to SPIN/traffic

	"add_block_node": argument is an integer containing a node number, which will be added to the list of IP addresses to be blocked.
	"add_ignore_node": argument is an integer containing a node number, which will be added to the list of IP addresses to be ignored.
	"add_allow_node": argument is an integer containing a node number, which will be added to the list of IP addresses to be allowed.
	
	"add_name": Sets a user-set name to a node. The argument is a map containing "node_id" (int) with the ID of the node, and "name" (string) with the name to be set.	
	
	"remove_block_node": argument is an integer containing a node number, which will be removed from the list of IP addresses to be blocked.
	"remove_ignore_node": argument is an integer containing a node number, which will be removed from the list of IP addresses to be ignored.
	"remove_allow_node": argument is an integer containing a node number, which will be removed from the list of IP addresses to be allowed.

	"remove_block_ip": argument is a string containing an IP address, which will be removed from the list of IP addresses to be blocked.
	"remove_ignore_ip": argument is a string containing an IP address, which will be removed from the list of IP addresses to be ignored.	
	"remove_allow_ip": argument is a string containing an IP address, which will be removed from the list of IP addresses to be allowed.	
	
	"reset_ignores": All ignores are removed, and replaced by the IP addresses of the system that SPIN is running on.
	
	"get_peak_info": Asks the peak detection to return information about a node; argument is the node id (int)

### Peak-based anomaly detection

As a provisional module, the Network Measurement Center can include an anomaly detection module.
This runs a basic peak detection algorithm, included as a demonstration of the options.

To obtain information about a particular node, send the "get_peak_info" command to SPIN/commands and give a node identifier as an argument.
If the peak detection is running, it will reply with the current limits of that device (in bytes/packets per minute), as well as the previously observed traffic (bytes/packets per minute) for the last hour.
The index goes from 0 to -59 for the last minute till 59 minutes ago, respectively.
An example is shown below.

    {
        "command": "peakinfo",
        "argument": "32",
        "result": {
            "items": {
                "0": {
                    "bytes": 5800,
                    "packets": 92
                },
                "-1": {
                    "bytes": 229932,
                    "packets": 1516
                },
                "-2": {
                    "bytes": 17232,
                    "packets": 213
                },
                "-3": {
                    "bytes": 57892,
                    "packets": 406
                },
                {...},
                "-59": {
                    "bytes": 200862,
                    "packets": 1426
                }
            },
            "maxbytes": 61388796,
            "maxpackets": 81543
        }
    }
