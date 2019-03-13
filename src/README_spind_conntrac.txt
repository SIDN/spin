
The conntrack variant of spind currently has a few system configuration
prerequisites;

- nf_conntrack kernel modules must be loaded, for general data collection
- nf accounting needs to be enabled, to show packet counts and sizes
- iptables needs to be set to send DNS packets to NFQUEUE

sudo modprobe nf_conntrack_ipv4
sudo modprobe nf_conntrack_ipv6
sudo sysctl net.netfilter.nf_conntrack_acct=1


# For running on a local system:

sudo iptables -I INPUT -p udp --sport 53 -j NFQUEUE --queue-bypass



# For running on a router:
