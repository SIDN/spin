# Usage of Mosquitto

We might be able to use Mosquitto more efficient while solving our current problem with big nodes

## Current Usage

We use two channels SPIN/commands and SPIN/traffic. Mosquitto uses a hierarchy of channels with a / as separator.

SPIN/commands is used as a sort of RPC procedure call channel, and SPIN/traffic contains traffic reports and results of requests done on the command channel.

We currently also use SPIN/stat for statistic reports but this could change.

## Mosquitto possibilities

Mosquitto allows you to subscribe to channels using wildcards, for example you could subscribe to SPIN/+ and receive messages for all current channels.

Furthermore Mosquitto allows for one message per channel that is retained, meaning that if a retained message is sent to channel x, while nobody is listening, as soon as someone subscribes to channel x he will get this retained message immediately followed by all new messages to channel x.

## Proposal

We split up the traffic channel:

- SPIN/traffic/flow
- SPIN/traffic/dns
- SPIN/traffic/node/node#
- SPIN/traffic/list/listname

Backwards compatibility for all current listsners could be to listen to SPIN/traffic/+ and get everything.

Information published:

On SPIN/traffic/list/allow the current allowed list as a retained message, meaning all listeners do not have to send a command anymore to get the latest list. Same of course for block and ignore and all other lists we can come up with.

On SPIN/traffic/node/1 the information about node 1. This is published as a retained message anytime the information about node 1 changes. Any listeners to SPIN/traffic/node/+ or more will always be up to date with the latest node info.

On SPIN/traffic/flow the flows seen recently. There will be no node information here, nodes will be just named by their number.

On SPIN/traffic/dns the DNS requests (and replies??). Again information about nodes will only be their number.

