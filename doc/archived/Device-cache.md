# Node cache vs device_cache thingies

## Node cache

Do we keep mac here? Name?
MAXNODES?
ip_refs?


Separate devices is really clumsy. Cannot use nodenum, because dependent on which nodecache...

Proposal:
Make device info a separate structure pointed at by selected nodes:

nodes are selected when they have a mac address, so mac address goes to device sub-struct. Name also?

The RPC's for devices have the mac-address as identifier, so we make a tree, lookup on mac address info is nodenum(or pointer).

When merging nodes, when one is a device merge into the device.
Both devices cannot be possible, since device is identified by mac address
