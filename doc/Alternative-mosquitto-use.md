# Usage of Mosquitto

We might be able to use Mosquitto more efficient while solving our current problem with big nodes, which would need  the traffic topic to be changed anyhow.

**This document is a proposal to be verified by stakeholders.**

## Current Usage

Mosquitto uses a hierarchy of topics with a / as separator.
We until recently used two topics *SPIN/commands* and *SPIN/traffic*. 

*SPIN/commands* is used as a sort of RPC procedure call topic, and *SPIN/traffic* contains traffic reports and results of requests done on the command topic.
We currently also use *SPIN/stat* for statistic reports but this could change.

## Mosquitto possibilities

Mosquitto allows you to subscribe to multiple topics.
This is done by either giving multiple subscribe commands, one per topic, or using wildcards, for example you could subscribe to SPIN/# and receive messages for all current topics.
When receiving messages you can, but do not have to, look at the topic they were posted to.

Furthermore Mosquitto allows for one message per topic that is retained, meaning that if a retained message is sent to topic x, even while nobody is listening, as soon as someone subscribes to topic x he will get this retained message immediately followed by all new messages to topic x.

This retained message can be deleted by posting an empty retained message.

## Proposal

We split up the traffic topic:

- SPIN/traffic/admin
- SPIN/traffic/flow
- SPIN/traffic/dns
- SPIN/traffic/node/(nodenum)
- SPIN/traffic/list/listname

Backwards compatibility for all current listeners could be to listen to *SPIN/traffic/#* and get everything.

Information published:

On *SPIN/traffic/admin* administrative messages, including but not limited to messages about nodes ending existence, either by being merged into other nodes, or by lack of traffic. Also the serverRestart message is a good one for this.

On *SPIN/traffic/list/allow* the current allowed list as a retained message, meaning all listeners do not have to send a command anymore to get the latest list. Same of course for block and ignore and all other lists we can come up with. Blocked flows perhaps?

On *SPIN/traffic/node/1* the information about node 1. This is published as a retained message anytime the information about node 1 changes, perhaps slightly delayed(TBD). Any listeners to *SPIN/traffic/node/+* or more will always be up to date with all the latest node info. When a node disapperas the message will be replaced by the empty string.

On *SPIN/traffic/flow* the flows seen recently. There will be no node information here, nodes will be just named by their number.

On *SPIN/traffic/dns* the DNS requests. Again information about nodes will only be their number.

For the statistics, assuming we keep them on Mosquitto, we could use something like: *SPIN/stat/module/counter* topics making it easy for someone just to query one counter. Making all counters retainable will get you the latest value with one call.