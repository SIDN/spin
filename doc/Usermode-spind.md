#Spind in usermode

Spind has been rewritten to run only in usermode using existing kernel hooks. This makes it easier to implement on new systems and reduces anxiety with vendors. A usermode program is less likely to crash your box...
To reach this point we had to change three packetflows that were handled by the kernel module
- The blocking of IP addresses and the reporting of blocked packets
- The catching of DNS packets
- Gathering statistics about packet flows

The implementation has been done, and the Daemon runs without a kernel module, and without the spin_config program doing initialization.

##Implement block-list with iptables
The blocking of IP addresses and the corresponding reporting is done with a combination of iptables chains and netfilter queues
###IPv4 only as example
IPv6 is done with same commands, but using ip6tables

###Ignore and except?
The old kernel module did not block packets depending on Block list but also on Ignore or Exept. Figure out intended meaning.
	- B is (Bs or Bd), same for I and E
	- If !B let through
	- if E let through with DEBUG print
	- if !I report DROP
	
Above behaviour is from a blocking perspective identical to:
- if E let through
- if B block
This document states the way to mimic exact same behaviour(minus the DEBUG print), but perhaps this should later become flow-based.

	
###Iptables setup

The idea is to make a set of chains to be called on each packet that will either block the packet(DROP in iptables lingo), or just return and let other chains decide. If the packet must be dropped another chain will make a decision on logging or not.

- Make a new chain, let us call it SpinCheck
	1 iptables -N SpinCheck
	
 - Add the chain to all standard chains
	1 iptables -I INPUT -j SpinCheck
	2 iptables -I OUTPUT -j SpinCheck
	3 iptables -I FORWARD -j SpinCheck
This puts this rule in front, is that always OK? Try to find out the best place.
	
- Make another chain for logging, let us call it  SpinLog. This chain will log except for ignored addresses.
	1 iptables -N SpinLog
	2 iptables -A SpinLog -j NFQUEUE --queue-num 2
The queueing command will stay at the end of the chain. We currently use queue number 2 for this. This will become configurable, but still the problem remains how we can use this while interacting with other software that uses Netfilter queues. The packets queued are handled by the Spin Daemon, which will report them and tell the kernel to DROP them.

- For each address a.b.c.d to be ignored jump back from beginning of SpinLog, so logging will not occur. Another rule(see below) will still drop them.
	1 iptable -I SpinLog -s a.b.c.d -j RETURN
	2 iptable -I SpinLog -d a.b.c.d -j RETURN
	
- Make another chain for actual blocking, let us call it SpinBlock. This list will end by calling SpinLog and then DROP the packet
	1 iptables -N SpinBlock
	2 iptables -A SpinBlock -j SpinLog
	3 iptables -A SpinBlock -j DROP
	
 - For each address a.b.c.d to be blocked let SpinCheck call SpinBlock.
	1 iptables -A SpinCheck -s a.b.c.d -j SpinBlock
	2 iptables -A SpinCheck -d a.b.c.d -j SpinBlock
This is the place to perhaps look at interfaces, and whether the address as source or destination matters.
	
- For each address a.b.c.d to be excepted let SpinBlock return to prevent blocking.
	1 iptables -I SpinBlock -s a.b.c.d -j RETURN
	2 iptables -I SpinBlock -d a.b.c.d -j RETURN
	
For unblock or unexcept or unignore do the same but with -D instead of -A or -I.

This is currently implemented with system("iptables ...") as a quick start. Optimization using lipiptc was contemplated but that seems to be not recommended. Some minor optimization is possible if needed.

This software will not use logging from the kernel, so there is no problem with growing files.

### Spin  Daemon code
Spin Daemon currently reads the queued packets and reports them through the Mosquitto interface. It reports the complete dropped packet, source, destination, ports et al. But there is not now, and nor was there with the old code, a way to see which address caused the block. Potentially that could also be aggregated, like periodic reports about how many times certain addresses were blocked, in which direction, on what interface. This is surely for the TODO list.

There is generic code in the SpinDaemon to read Netfilter Queues with a software registration system where software subsystems can register to get the packets they are  interested in. This is currently used both by the blocking code and by the DNS code.

### DNS code
Uses a similar iptables construct as above.
	1 iptables -A SpinCheck -p udp --sport 53 NFQUEUE --queue-bypass --queue-num 1
	2 iptables -A SpinCheck -p udp --sport 53 NFQUEUE --queue-bypass --queue-num 1
	
The queue-bypass option makes sure that if the SpinDaemon crashes packets will be passed on. Without that option they would be dropped, which of course was fine in the dropping code above. We currently use queue number 1. Again, this will be configurable. Again, it uses the system("iptables ...") implementation at the moment.

### Traffic accounting code
Uses the kernel netfilter_conntrack module.

To start external to the Spin Daemon the following initialization takes place:

 - modprobe nf_conntrack
 - modprobe nf_conntrack_ipv4
 - modprobe nf_conntrack_ipv6
 - modprobe nf_netlink_queue
 - sysctl net.netfilter.nf_conntrack_acct=1
 
 The software subsystem in the Spin Daemon regularly(currently once per second) reads the gathered statistics from the kernel and rezeroes the counters.



