# RPC mechanisms
Various RPC mechanism are used and can be used in the SPIN system. A short discussion.

## Now

The spind program (ab)uses the Mosquitto pub/sub system as an RPC mechanism, by sending JSON messages over the SPIN/commands channel. There are no real returns for this RPC, except that all commands to change lists send the new value of the list on the traffic channel.

The web_ui uses a REST API to send commands with parameters partly encoded in the URL and partly sent as JSON data. Results are sent in JSON format over HTTP.

## Other mechanisms

### Ubus

OpenWRT would like us to use ubus as an RPC mechanism. Ubus is a mechanism using serialized data structures, like JSON, but more compact and binary.
Ubus rendez-vous is through a local ubusd program, so not networked. There is however a mechanism to connect through the uhttpd process somehow, which should make it available over the network.
Ubus contains a sort of broadcast mechanism to all listeners.

The ubus system has a set of shell commands to talk to ubusd where all the calls are encoded in JSON. Easy for testing and interfacing to scripts.

### JSON-RPC

A generic type of RPC mechanism called JSON-RPC is used in various projects. JSON-RPC is a relatively simple mechanism, and is transport agnostic. JSON-RPC often runs on HTTP making it look like the REST API of the web_ui. JSON-RPC could also run on simple TCP sockets, but there problems of rendez-vous and message delimitation have no standard solution.

There is even a document explaining how to use JSON-RPC on top of Mosquitto making it look like the bubble-app to spind mechanism.

JSON-RPC contains a notify mechanism, which is an RPC without reult.

JSON-RPC can be accessed using the standard *curl* program. Easy for testing and interfacing to scripts.

### Comparison

Ubus and JSON-RPC seem to be more or less identical in expressability.
JSON-RPC is more standard than Ubus, and is easier to run over a network, but it does not matter that much.

Marshalling and de-marshalling code will undoubtedly differ and will have to be hidden from the rest of the code somehow. With the real RPC calls in SPIN arguments and results are relatively straightforward. The complex stuff such as nodes and flows currently only occur in the traffic mechanism.

## How to progress
We should probably first define all our RPC calls in a way that does not depend on the RPC mechanism, let alone the transport. As long as the call and the return are implementable in JSON.

Then we could choose our RPC mechanism depending on platform, just like we choose the configuration mechanism now.

If we really go wild we could make a translation process that interfaces between RPC mechanisms.

## Performance considerations
As far as I can see now we do not need to care very much about performance issues here. At least now we do not do too many RPC's per second.
