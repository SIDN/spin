The spin_mqtt.lua daemon will send traffic infromation to the topic
SPIN/traffic. It will listen on the topic SPIN/commands for commands.

All messages are of the format

    {
       "command": <command name (string)>,
       "argument": <argument(s) (dynamic)>,
       "result": <result(s) (dynamic)>
    }

On the SPIN/traffic topic, commands can be one of:

* "traffic": Traffic flow information, see next section
* "nodeUpdate": Updates information about nodes that were seen earlier
  (such as an additional domain name that resolved to the same IP
  address, or a new IP address that is matched to an ARP address that has
  been seen before). See below for the format.
* "serverRestart": Tells the client that the server has restarted and
  it should drop its local cache of nodes. argument and result are empty.
* "filters": result is a list of strings containing IP addresses that are
  currently filtered (not shown) by the SPIN system.
* "names": result is a map containing IP address -> domain name values;
  these are user-set names.



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
                }
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
                }
             }
          ],
          "timestamp":1497622538,
          "total_size":117,
          "total_count":2
       }
    }

### Node update

A node update contains the same information as a node element from the
previous section; it contains (additional) information about a node
that has been seen earlier.

### Node update example

    {
       "command":"nodeUpdate",
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

### Configuration commands

The client can send commands to the SPIN daemon on the 'SPIN/commands'
topic. These usually have no "result" value, but often do contain an
"argument" section.

The following commands can be issued:

* "get_filters": Triggers a new "filters" message to be sent to SPIN/traffic
* "add_filter": argument is a string containing an IP address, which will be added to the list of IP addresses to be filtered.
* "remove_filter": argument is a string containing an IP address, which will be removed from the list of IP addresses to be filtered.
* "reset_filters": All filters are removed, and replaced by the IP addresses of the system that SPIN is running on.
* "get_names": Triggers a new "names" message to be sent to SPIN/traffic
* "add_name": Sets a user-set name to a node. The argument is a map containing "node_id" (int) with the ID of the node, and "name" (string) with the name to be set.
* "blockdata": Tells the SPIN system to block all traffic from and to a node; argument is the node id (int)
* "stopblockdata": Tells the SPIN system to stop blocking traffic to and from a node; argument is the node id (int)
* "allowdata": Tells the SPIN system to accept all traffic from and to a node, even though this traffic would be blocked otherwise (for instance, if the traffic is from a node that was blocked); argument is the node id (int)
* "stopallowdata": Tells the SPIN system to no longer accept all traffic from and to a node, even though this traffic would be blocked otherwise (for instance, if the traffic is from a node that was blocked); argument is the node id (int)

